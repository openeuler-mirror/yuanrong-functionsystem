import os
import csv
import stat
import utils
import tarfile
import zipfile
import hashlib
import argparse
import urllib.request

log = utils.init_logger()


def download_vendor(config_path, download_path):
    """主函数"""
    config_path = os.path.abspath(config_path)
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"Configuration file not found: {config_path}")

    download_path = os.path.abspath(download_path)
    os.makedirs(download_path, exist_ok=True)

    reader = csv.DictReader(open(config_path, mode="r", encoding="utf-8"))
    configs = list(reader)  # name, version, module, repo, sha256

    for config in configs:
        archive_name = config['repo'].split('/')[-1]
        package_name = archive_name.replace(".tar.gz", "").replace(".tar", "").replace(".zip", "")
        archive_path = os.path.join(download_path, archive_name)  # vendor/xxx-vvv.zip
        vendor_path = os.path.join(download_path, config['name'])  # vendor/xxx

        if os.path.exists(vendor_path):
            log.info(f"Dependency {config['name']}-{config['version']} already exists, skipping download and extraction")
            continue

        log.info(f"Downloading {config['name']}-{config['version']} with checksum: {config['sha256']} from source: {config['repo']}")
        download_zipfile(package_name, config['repo'], archive_path)
        verify_checksum(package_name, archive_path, config['sha256'])
        extract_name = extract_file(archive_path, download_path)
        package_path = os.path.join(download_path, extract_name)  # vendor/xxx-vvv
        os.rename(package_path, vendor_path)
    return 0


def download_zipfile(package_name, download_url, download_path):
    """下载文件到指定路径"""
    try:
        file = open(download_path, "wb")
        headers = {'User-Agent': 'curl/7.68.0'}
        req = urllib.request.Request(download_url, headers=headers, method='GET')
        resp = urllib.request.urlopen(req)
        file_size = int(resp.getheader('Content-Length', 0))
        downloaded = 0
        block_size = 8192

        while True:
            buffer = resp.read(block_size)
            if not buffer:
                break
            downloaded += len(buffer)
            file.write(buffer)
        file.close()
        log.info(f"Dependency {package_name} downloaded successfully: Total {downloaded}/{file_size} bytes")
    except Exception as e:
        log.info(f"Failed to download dependency {package_name}: {str(e)}")
        raise


def verify_checksum(package_name, file_path, expected_sha256):
    """验证文件校验和"""
    actual_sha256 = compute_sha256(file_path)
    if actual_sha256 == expected_sha256:
        log.info(f"Dependency {package_name} hash verification successful, hash value: {expected_sha256}")
    else:
        log.info(f"Dependency {package_name} hash verification failed. Expected: {expected_sha256}, Actual: {actual_sha256}")
        raise ValueError("Download verification failed")


def compute_sha256(file_path):
    """计算文件的SHA256校验值"""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()


def extract_file(archive_path, extract_to):
    if archive_path.endswith(".zip"):
        return extract_zip(archive_path, extract_to)
    elif archive_path.endswith(".tar.gz"):
        return extract_tar(archive_path, extract_to)
    elif archive_path.endswith(".tar"):
        return extract_tar(archive_path, extract_to)
    else:
        raise TypeError(f"Unsupported compression type for file {archive_path.split('/')[-1]}")


def extract_zip(zipfile_path, extract_to):
    """解压ZIP文件到指定目录"""
    zip_ref = zipfile.ZipFile(zipfile_path, 'r')
    zip_ref.extractall(extract_to)
    for info in zip_ref.infolist():
        extracted_path = os.path.join(extract_to, info.filename)
        if not info.is_dir() and info.external_attr:
            unix_permissions = (info.external_attr >> 16) & 0o777
            os.chmod(extracted_path, unix_permissions)
    for info in zip_ref.infolist():
        file_path = os.path.join(extract_to, info.filename)
        if _is_symlink(info):
            # 读取并创建符号链接
            link_target = zip_ref.read(info.filename).decode('utf-8').strip()
            if os.path.lexists(file_path):
                os.remove(file_path)
            os.symlink(link_target, file_path)
    log.info(f"File {zipfile_path.split('/')[-1]} extraction complete")
    return zip_ref.infolist()[0].filename.split('/')[0]


def extract_tar(tarfile_path, extract_to):
    """解压ZIP文件到指定目录"""
    tar_ref = tarfile.open(tarfile_path, 'r:*')
    tar_ref.extractall(extract_to)
    log.info(f"File {tarfile_path.split('/')[-1]} extraction complete")
    return tar_ref.getnames()[0]


def _is_symlink(info):
    """检查 ZIP 条目是否为符号链接"""
    mode = (info.external_attr >> 16) & 0xFFFF
    return stat.S_IFMT(mode) == stat.S_IFLNK


if __name__ == "__main__":
    _parser = argparse.ArgumentParser(description='Compilate vendor dependencies pre-downloader')
    _parser.add_argument('--config', type=str, required=True, help='Configuration file path')
    _parser.add_argument('--output', type=str, required=True, help='Dependencies download path')
    _args = _parser.parse_args()
    _code = download_vendor(_args.config, _args.output)
    exit(_code)
