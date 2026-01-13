#!/usr/bin/env python3
"""
Manage etcd instances: query, analyze, and clean up failed instances.
Delete instances with specific status codes and their corresponding route keys.
"""
import json
import os
import subprocess
import sys
from collections import defaultdict
from typing import List, Tuple, Dict, Optional
from urllib import request, error
from urllib.parse import urljoin

# Default excluded instance ID prefixes
DEFAULT_EXCLUDED_PREFIXES = ['driver-faas-frontend', 'driver-scheduler']


def parse_master_info_file(file_path: str) -> Dict[str, str]:
    """
    Parse master info file and return configuration dictionary.
    File format: key1:value1,key2:value2,...

    Example formats:
    - local_ip:172.17.0.2,master_ip:172.17.0.2,etcd_ip:172.17.0.2,etcd_port:11673,...
    - etcd_addr_list:etcd.akernel-test.svc.sa127-sqa-mainsite,local_ip:172.17.0.2,...
    """
    config = {}
    try:
        with open(file_path, 'r') as f:
            content = f.read().strip()

        # Parse key:value pairs separated by comma
        pairs = content.split(',')
        for pair in pairs:
            pair = pair.strip()
            if ':' in pair:
                key, value = pair.split(':', 1)
                config[key.strip()] = value.strip()

    except FileNotFoundError:
        print(f"Warning: Master info file not found: {file_path}", file=sys.stderr)
    except Exception as e:
        print(f"Warning: Failed to parse master info file: {e}", file=sys.stderr)

    return config


def setup_from_master_info() -> Tuple[Optional[str], Optional[str]]:
    """
    Setup etcd endpoints and resources URL from MASTER_INFO environment variable.
    Returns (etcdctl_endpoints, resources_url) or (None, None) if not configured.

    The function reads the file path from MASTER_INFO environment variable,
    parses the configuration, and extracts:
    - etcd_addr_list or etcd_ip + etcd_port -> ETCDCTL_ENDPOINTS
    - master_ip + global_scheduler_port -> resources_url
    """
    master_info_path = os.environ.get('MASTER_INFO')
    if not master_info_path:
        return None, None

    config = parse_master_info_file(master_info_path)
    if not config:
        return None, None

    # Get etcd address - prefer etcd_addr_list, fallback to etcd_ip
    etcd_addr = config.get('etcd_addr_list') or config.get('etcd_ip')
    etcd_port = config.get('etcd_port')

    etcdctl_endpoints = None
    if etcd_addr and etcd_port:
        etcdctl_endpoints = f"{etcd_addr}:{etcd_port}"
        print(f"Configured ETCDCTL_ENDPOINTS from MASTER_INFO: {etcdctl_endpoints}")

    # Get resources URL
    master_ip = config.get('master_ip')
    global_scheduler_port = config.get('global_scheduler_port')

    resources_url = None
    if master_ip and global_scheduler_port:
        resources_url = f"http://{master_ip}:{global_scheduler_port}"
        print(f"Configured resources URL from MASTER_INFO: {resources_url}")

    return etcdctl_endpoints, resources_url


def run_etcdctl(args: List[str]) -> Tuple[int, str, str]:
    """Run etcdctl command and return exit code, stdout, stderr."""
    cmd = ['etcdctl'] + args
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "Command timed out"
    except FileNotFoundError:
        return -1, "", "etcdctl not found. Please install etcd client."
    except Exception as e:
        return -1, "", str(e)


def get_keys_with_prefix(prefix: str) -> List[Tuple[str, str]]:
    """
    Get all keys with given prefix from etcd.
    Returns list of (key, value) tuples.
    """
    code, stdout, stderr = run_etcdctl([
        'get', prefix,
        '--prefix',
        '--print-value-only=false'
    ])

    if code != 0:
        print(f"Error getting keys: {stderr}", file=sys.stderr)
        return []

    lines = stdout.strip().split('\n')
    result = []

    # etcdctl output format: key\nvalue\nkey\nvalue\n...
    for i in range(0, len(lines), 2):
        if i + 1 < len(lines):
            key = lines[i]
            value = lines[i + 1]
            result.append((key, value))

    return result


def delete_key(key: str, dry_run: bool = False) -> bool:
    """Delete a key from etcd."""
    if dry_run:
        print(f"[DRY-RUN] Would delete key: {key}")
        return True

    code, stdout, stderr = run_etcdctl(['del', key])

    if code != 0:
        print(f"Error deleting key {key}: {stderr}", file=sys.stderr)
        return False

    print(f"Deleted key: {key}")
    return True


def query_resources(args):
    """Query and display resources information from the scheduler."""
    url = urljoin(args.resources_url, '/global-scheduler/resources')
    print(f"Querying resources from: {url}")

    try:
        req = request.Request(url, method='GET')
        # Request JSON format
        req.add_header('Type', 'json')

        with request.urlopen(req, timeout=30) as response:
            if response.status != 200:
                print(f"Error: HTTP {response.status}", file=sys.stderr)
                return 1

            data = json.loads(response.read().decode('utf-8'))

            if args.verbose:
                print("\n" + "=" * 100)
                print("Raw JSON Response:")
                print("=" * 100)
                print(json.dumps(data, indent=2, ensure_ascii=False))
                print("\n")

            # Parse the resource structure
            request_id = data.get('requestID', 'N/A')
            resource = data.get('resource', {})

            if not resource:
                print("No resource information found.", file=sys.stderr)
                return 1

            # Display global resource information
            print("\n" + "=" * 80)
            print("Global Resource Information")
            print("=" * 80)
            print(f"Request ID: {request_id}")
            print(f"Resource ID: {resource.get('id', 'N/A')}")
            print(f"Owner ID: {resource.get('ownerId', 'N/A')}")
            print(f"Revision: {resource.get('revision', 'N/A')}")

            # Display global capacity and allocatable
            capacity = resource.get('capacity', {}).get('resources', {})
            allocatable = resource.get('allocatable', {}).get('resources', {})

            print("\n" + "-" * 80)
            print(f"{'Resource Type':<20} {'Capacity':<30} {'Allocatable':<30}")
            print("-" * 80)

            # Collect all resource types
            resource_types = set()
            resource_types.update(capacity.keys())
            resource_types.update(allocatable.keys())

            for res_type in sorted(resource_types):
                cap_val = capacity.get(res_type, {}).get('scalar', {}).get('value', 0)
                alloc_val = allocatable.get(res_type, {}).get('scalar', {}).get('value', 0)

                # Skip NPU/ type resources in global view (shown in fragment details)
                if res_type.startswith('NPU/'):
                    continue

                print(f"{res_type:<20} {cap_val:<30} {alloc_val:<30}")

            # Display fragment resources in table format
            fragments = resource.get('fragment', {})
            if fragments:
                print("\n" + "=" * 150)
                print("Fragment Resources (Nodes)")
                print("=" * 150)
                print(f"Total Nodes: {len(fragments)}\n")

                # Collect all resource types from all fragments
                all_resource_types = set()
                for node_data in fragments.values():
                    node_capacity = node_data.get('capacity', {}).get('resources', {})
                    node_allocatable = node_data.get('allocatable', {}).get('resources', {})
                    all_resource_types.update(node_capacity.keys())
                    all_resource_types.update(node_allocatable.keys())

                # Remove NPU/ type resources
                all_resource_types = [rt for rt in sorted(all_resource_types) if not rt.startswith('NPU/')]

                # Build table header
                header = f"{'Node ID':<40} {'Host IP':<20}"
                for res_type in all_resource_types:
                    header += f" {res_type + ' Cap':<15} {res_type + ' Alloc':<15}"
                print(header)
                print("-" * 150)

                # Display each node as a row
                for node_id, node_data in fragments.items():
                    # Get node labels
                    node_labels = node_data.get('nodeLabels', {})
                    host_ip_items = node_labels.get('HOST_IP', {}).get('items', {})
                    host_ips = ', '.join(host_ip_items.keys()) if host_ip_items else 'N/A'

                    # Truncate node_id if too long
                    display_node_id = node_id[:38] + '..' if len(node_id) > 40 else node_id
                    display_host_ip = host_ips[:18] + '..' if len(host_ips) > 20 else host_ips

                    row = f"{display_node_id:<40} {display_host_ip:<20}"

                    # Get resource values for this node
                    node_capacity = node_data.get('capacity', {}).get('resources', {})
                    node_allocatable = node_data.get('allocatable', {}).get('resources', {})

                    for res_type in all_resource_types:
                        cap_val = node_capacity.get(res_type, {}).get('scalar', {}).get('value', 0)
                        alloc_val = node_allocatable.get(res_type, {}).get('scalar', {}).get('value', 0)
                        row += f" {cap_val:<15} {alloc_val:<15}"

                    print(row)

                print()

            print("=" * 150)
            return 0

    except error.HTTPError as e:
        print(f"HTTP Error: {e.code} - {e.reason}", file=sys.stderr)
        if args.verbose:
            print(f"Response: {e.read().decode('utf-8')}", file=sys.stderr)
        return 1
    except error.URLError as e:
        print(f"URL Error: {e.reason}", file=sys.stderr)
        return 1
    except json.JSONDecodeError as e:
        print(f"JSON decode error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error querying resources: {e}", file=sys.stderr)
        if args.verbose:
            import traceback
            traceback.print_exc()
        return 1


def query_instances(args):
    """Query and display all instances matching the criteria."""
    print(f"Querying instances with prefix: {args.instance_prefix}")
    instances = get_keys_with_prefix(args.instance_prefix)

    if not instances:
        print("No instances found.")
        return 0

    print(f"Found {len(instances)} total instance(s)\n")

    # Group instances by status code and NodeID
    by_status = {}
    by_proxy_id: Dict[str, List] = defaultdict(list)
    matching_instances = []
    excluded_count = 0

    for key, value in instances:
        try:
            data = json.loads(value)
        except json.JSONDecodeError as e:
            print(f"Warning: Failed to parse JSON for key {key}: {e}", file=sys.stderr)
            continue

        instance_id = data.get('instanceID', '')

        # Skip instances with excluded prefixes
        should_exclude = False
        for prefix in DEFAULT_EXCLUDED_PREFIXES:
            if instance_id.startswith(prefix):
                should_exclude = True
                excluded_count += 1
                break

        if should_exclude:
            continue

        instance_status = data.get('instanceStatus', {})
        status_code = instance_status.get('code', 'N/A')
        proxy_id = data.get('functionProxyID', 'N/A')

        # Count by status
        if status_code not in by_status:
            by_status[status_code] = []
        by_status[status_code].append(data)

        # Group by proxy ID
        by_proxy_id[proxy_id].append((status_code, data))

        # Collect matching instances
        # Filter by instance_id if specified
        if args.instance_id:
            if instance_id == args.instance_id:
                matching_instances.append((key, data))
        # Filter by status_code if specified
        elif args.status_code is not None:
            if status_code == args.status_code:
                matching_instances.append((key, data))
        # No filter, show all
        else:
            matching_instances.append((key, data))

    # Display summary by status
    print("=" * 100)
    print("Instance Summary by Status Code:")
    print("=" * 100)
    if excluded_count > 0:
        print(f"Note: Excluded {excluded_count} instance(s) with prefixes: {', '.join(DEFAULT_EXCLUDED_PREFIXES)}\n")
    for status_code in sorted(by_status.keys(), key=lambda x: (x != args.status_code, x)):
        count = len(by_status[status_code])
        marker = " <- TARGET" if status_code == args.status_code else ""
        print(f"  Status Code {status_code}: {count} instance(s){marker}")

    # Display NodeID statistics table
    print("\n" + "=" * 100)
    print("Instance Statistics by NodeID:")
    print("=" * 100)

    # Prepare table data
    table_data = []
    for proxy_id, instances_list in by_proxy_id.items():
        status_counts = defaultdict(int)
        functions = set()
        notes = set()
        tenant_ids = set()

        for status_code, data in instances_list:
            status_counts[status_code] += 1
            function_name = data.get('function', 'N/A')
            functions.add(function_name)
            tenant_ids.add(data.get('tenantID', 'N/A'))

            # Get note from createOptions, fallback to function name
            create_options = data.get('createOptions', {})
            if create_options and 'FUNCTION_KEY_NOTE' in create_options:
                note = create_options['FUNCTION_KEY_NOTE']
            else:
                note = function_name
            notes.add(note)

        table_data.append({
            'proxy_id': proxy_id,
            'total': len(instances_list),
            'status_counts': status_counts,
            'functions': ', '.join(sorted(functions)),
            'notes': ', '.join(sorted(notes)),
            'tenant_ids': ', '.join(sorted(tenant_ids))
        })

    # Sort by total count (descending)
    table_data.sort(key=lambda x: x['total'], reverse=True)

    # Print table header
    print(f"{'NodeID':<40} {'Total':<8} {'Status Distribution':<30} {'Function/Note'}")
    print("-" * 100)

    # Print table rows
    for row in table_data:
        proxy_id = row['proxy_id'][:38] + '..' if len(row['proxy_id']) > 40 else row['proxy_id']
        total = row['total']

        # Format status distribution
        status_parts = [f"[{k}]:{v}" for k, v in sorted(row['status_counts'].items())]
        status_str = ' '.join(status_parts)[:28]

        note = row['notes'][:48] + '..' if len(row['notes']) > 50 else row['notes']

        print(f"{proxy_id:<40} {total:<8} {status_str:<30} {note}")

    print("=" * 100)

    # Display statistics by Function/Note
    print("\n" + "=" * 100)
    print("Instance Statistics by Function/Note:")
    print("=" * 100)

    # Prepare table data grouped by Function/Note
    by_note: Dict[str, List] = defaultdict(list)
    for key, value in instances:
        try:
            data = json.loads(value)
        except json.JSONDecodeError:
            continue

        instance_id = data.get('instanceID', '')

        # Skip instances with excluded prefixes
        should_exclude = False
        for prefix in DEFAULT_EXCLUDED_PREFIXES:
            if instance_id.startswith(prefix):
                should_exclude = True
                break

        if should_exclude:
            continue

        instance_status = data.get('instanceStatus', {})
        status_code = instance_status.get('code', 'N/A')
        function_name = data.get('function', 'N/A')

        # Get note from createOptions, fallback to function name
        create_options = data.get('createOptions', {})
        if create_options and 'FUNCTION_KEY_NOTE' in create_options:
            note = create_options['FUNCTION_KEY_NOTE']
        else:
            note = function_name

        by_note[note].append((status_code, data))

    # Prepare table data
    note_table_data = []
    for note, instances_list in by_note.items():
        status_counts = defaultdict(int)
        proxy_ids = set()
        tenant_ids = set()

        for status_code, data in instances_list:
            status_counts[status_code] += 1
            proxy_ids.add(data.get('functionProxyID', 'N/A'))
            tenant_ids.add(data.get('tenantID', 'N/A'))

        note_table_data.append({
            'note': note,
            'total': len(instances_list),
            'status_counts': status_counts,
            'node_count': len(proxy_ids),
            'tenant_ids': ', '.join(sorted(tenant_ids))
        })

    # Sort by total count (descending)
    note_table_data.sort(key=lambda x: x['total'], reverse=True)

    # Print table header
    print(f"{'Function/Note':<50} {'Total':<8} {'Nodes':<8} {'Status Distribution':<30}")
    print("-" * 100)

    # Print table rows
    for row in note_table_data:
        note = row['note'][:48] + '..' if len(row['note']) > 50 else row['note']
        total = row['total']
        node_count = row['node_count']

        # Format status distribution
        status_parts = [f"[{k}]:{v}" for k, v in sorted(row['status_counts'].items())]
        status_str = ' '.join(status_parts)[:28]

        print(f"{note:<50} {total:<8} {node_count:<8} {status_str}")

    print("=" * 100)

    # Display matching instances detail (only if --show-details is enabled)
    if args.show_details:
        if not matching_instances:
            if args.instance_id:
                filter_msg = f" with instance ID {args.instance_id}"
            elif args.status_code is not None:
                filter_msg = f" with status code {args.status_code}"
            else:
                filter_msg = ""
            print(f"\nNo instances{filter_msg} found for detailed display.")
        else:
            print("\n" + "=" * 100)
            if args.instance_id:
                filter_msg = f" (Instance ID: {args.instance_id})"
            elif args.status_code is not None:
                filter_msg = f" (Status Code: {args.status_code})"
            else:
                filter_msg = ""
            print(f"Matching Instance Details{filter_msg}:")
            print("=" * 100)

            for idx, (key, data) in enumerate(matching_instances, 1):
                instance_id = data.get('instanceID', 'N/A')
                proxy_id = data.get('functionProxyID', 'N/A')
                function = data.get('function', 'N/A')
                status_code = data.get('instanceStatus', {}).get('code', 'N/A')
                tenant_id = data.get('tenantID', 'N/A')
                is_system = data.get('isSystemFunc', False)
                labels = data.get('labels', {})
                create_options = data.get('createOptions', {})

                print(f"\n[{idx}] NodeID: {proxy_id}")
                print(f"    Instance ID: {instance_id}")
                print(f"    Function: {function}")
                print(f"    Status Code: {status_code}")
                print(f"    Tenant ID: {tenant_id}")
                print(f"    System Func: {is_system}")

                # Display note from createOptions if available
                if create_options and 'FUNCTION_KEY_NOTE' in create_options:
                    note = create_options['FUNCTION_KEY_NOTE']
                    print(f"    Note: {note}")
            print("=" * 100)
    else:
        if args.instance_id:
            filter_msg = f" with instance ID {args.instance_id}"
        elif args.status_code is not None:
            filter_msg = f" with status code {args.status_code}"
        else:
            filter_msg = ""
        print(f"\nTotal matching instances{filter_msg}: {len(matching_instances)}")
        print("Use --show-details to display individual instance information.")

    return 0


def main():
    import argparse

    # Setup configuration from MASTER_INFO environment variable
    etcdctl_endpoints, resources_url = setup_from_master_info()

    # Set ETCDCTL_ENDPOINTS environment variable if configured
    if etcdctl_endpoints:
        os.environ['ETCDCTL_ENDPOINTS'] = etcdctl_endpoints

    # Use configured resources_url or default
    default_resources_url = resources_url or 'http://127.0.0.1:8080'

    parser = argparse.ArgumentParser(
        description='Query and clean up instances from etcd',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Query all instances (show summary table only)
  %(prog)s --query

  # Query instances with specific status code
  %(prog)s --query --status-code 6

  # Query specific instance by ID
  %(prog)s --query --instance-id abc123 --show-details

  # Show detailed instance information
  %(prog)s --query --status-code 6 --show-details

  # Query with verbose output (show full JSON)
  %(prog)s --query --status-code 6 --show-details --verbose

  # Query resources information from scheduler
  %(prog)s --query-resources

  # Query resources with custom URL and verbose output
  %(prog)s --query-resources --resources-url http://localhost:8080 --verbose

  # Dry run delete (preview what would be deleted)
  %(prog)s --delete --status-code 6 --dry-run

  # Actually delete instances with status code 6
  %(prog)s --delete --status-code 6

  # Delete specific instance by instance ID
  %(prog)s --delete --instance-id abc123

  # Delete with custom excluded prefixes
  %(prog)s --delete --status-code 6 --exclude-prefixes driver-faas driver-sched

  # Delete without any exclusions
  %(prog)s --delete --status-code 6 --exclude-prefixes
        """
    )
    parser.add_argument(
        '--query',
        action='store_true',
        help='Query and display instances without deleting'
    )
    parser.add_argument(
        '--query-resources',
        action='store_true',
        help='Query and display resources information from scheduler'
    )
    parser.add_argument(
        '--delete',
        action='store_true',
        help='Delete instances matching the criteria'
    )
    parser.add_argument(
        '--instance-prefix',
        default='/sn/instance/',
        help='Prefix for instance keys (default: /sn/instance/)'
    )
    parser.add_argument(
        '--route-prefix',
        default='/yr/route/business/yrk/',
        help='Prefix for route keys (default: /yr/route/business/yrk/)'
    )
    parser.add_argument(
        '--resources-url',
        default=default_resources_url,
        help=f'Base URL for resources API (default: {default_resources_url}, auto-configured from MASTER_INFO if available)'
    )
    parser.add_argument(
        '--status-code',
        type=int,
        default=None,
        help='Instance status code to filter (default: None, show all)'
    )
    parser.add_argument(
        '--instance-id',
        type=str,
        default=None,
        help='Specific instance ID to delete'
    )
    parser.add_argument(
        '--exclude-prefixes',
        type=str,
        nargs='+',
        default=None,
        help=f'Instance ID prefixes to exclude from deletion (default: {" ".join(DEFAULT_EXCLUDED_PREFIXES)})'
    )
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Show what would be deleted without actually deleting'
    )
    parser.add_argument(
        '--verbose',
        '-v',
        action='store_true',
        help='Verbose output (show full JSON when combined with --show-details)'
    )
    parser.add_argument(
        '--show-details',
        action='store_true',
        help='Show detailed information for each instance (default: only show summary table)'
    )

    args = parser.parse_args()

    # Must specify either --query, --query-resources, or --delete
    if not args.query and not args.delete and not args.query_resources:
        parser.error("Must specify either --query, --query-resources, or --delete")

    operation_count = sum([args.query, args.delete, args.query_resources])
    if operation_count > 1:
        parser.error("Cannot use multiple operations (--query, --query-resources, --delete) at the same time")

    # If query resources mode, display and exit
    if args.query_resources:
        return query_resources(args)

    # If query mode, just display and exit
    if args.query:
        return query_instances(args)

    # Delete mode - require either status-code or instance-id
    if args.status_code is None and not args.instance_id:
        parser.error("Either --status-code or --instance-id is required for delete operations")

    if args.status_code is not None and args.instance_id:
        parser.error("Cannot use both --status-code and --instance-id at the same time")

    # Parse excluded prefixes
    excluded_prefixes = args.exclude_prefixes if args.exclude_prefixes else DEFAULT_EXCLUDED_PREFIXES

    print(f"Fetching instances with prefix: {args.instance_prefix}")
    instances = get_keys_with_prefix(args.instance_prefix)

    if not instances:
        print("No instances found.")
        return 0

    print(f"Found {len(instances)} instance(s)")
    if excluded_prefixes:
        print(f"Excluded prefixes: {', '.join(excluded_prefixes)}")

    to_delete = []
    excluded_count = 0

    for key, value in instances:
        try:
            data = json.loads(value)
        except json.JSONDecodeError as e:
            print(f"Warning: Failed to parse JSON for key {key}: {e}", file=sys.stderr)
            if args.verbose:
                print(f"  Value: {value[:100]}...", file=sys.stderr)
            continue

        instance_id = data.get('instanceID', '')

        # Check if instance ID should be excluded
        should_exclude = False
        if excluded_prefixes and instance_id:
            for prefix in excluded_prefixes:
                if instance_id.startswith(prefix):
                    should_exclude = True
                    excluded_count += 1
                    if args.verbose:
                        print(f"\nExcluding instance: {instance_id} (matches prefix: {prefix})")
                    break

        if should_exclude:
            continue

        # Delete by instance ID
        if args.instance_id:
            if instance_id == args.instance_id:
                to_delete.append((key, instance_id))
                if args.verbose:
                    print(f"\nFound matching instance:")
                    print(f"  Key: {key}")
                    print(f"  Instance ID: {instance_id}")
                    print(f"  Function: {data.get('function', 'N/A')}")
        # Delete by status code
        elif args.status_code is not None:
            instance_status = data.get('instanceStatus', {})
            status_code = instance_status.get('code')

            if status_code == args.status_code:
                to_delete.append((key, instance_id))

                if args.verbose:
                    print(f"\nFound matching instance:")
                    print(f"  Key: {key}")
                    print(f"  Instance ID: {instance_id}")
                    print(f"  Status Code: {status_code}")
                    print(f"  Function: {data.get('function', 'N/A')}")

    if not to_delete:
        if args.instance_id:
            print(f"\nInstance with ID '{args.instance_id}' not found.")
        else:
            print(f"\nNo instances with status code {args.status_code} found.")
        if excluded_count > 0:
            print(f"Note: {excluded_count} instance(s) were excluded by prefix filter.")
        return 0

    print(f"\n{'[DRY-RUN] ' if args.dry_run else ''}Found {len(to_delete)} instance(s) to delete:")
    if excluded_count > 0:
        print(f"Note: {excluded_count} instance(s) were excluded by prefix filter.")

    deleted_count = 0
    failed_count = 0

    for instance_key, instance_id in to_delete:
        print(f"\n{'[DRY-RUN] ' if args.dry_run else ''}Processing instance: {instance_id}")

        # Delete instance key
        if delete_key(instance_key, args.dry_run):
            deleted_count += 1
        else:
            failed_count += 1
            continue

        # Delete corresponding route key if instanceID exists
        if instance_id:
            route_key = f"{args.route_prefix}{instance_id}"
            if delete_key(route_key, args.dry_run):
                deleted_count += 1
            else:
                failed_count += 1

    print(f"\n{'[DRY-RUN] ' if args.dry_run else ''}Summary:")
    print(f"  Keys deleted: {deleted_count}")
    if failed_count > 0:
        print(f"  Keys failed: {failed_count}")

    if args.dry_run:
        print("\nThis was a dry run. Use without --dry-run to actually delete.")

    return 0 if failed_count == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
