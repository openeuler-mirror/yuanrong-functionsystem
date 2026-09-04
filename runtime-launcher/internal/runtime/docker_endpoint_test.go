package runtime

import (
	"reflect"
	"testing"

	networktypes "github.com/docker/docker/api/types/network"
)

func TestCloneCommandPreservesStructuredArgv(t *testing.T) {
	want := []string{
		"python3.9",
		"-u",
		"-c",
		"import os,sys; os.execv(sys.executable, [sys.executable] + sys.argv[1:])",
		"--rt_server_address",
		"10.0.0.2:22773",
	}
	got := cloneCommand(want)
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("cloneCommand() = %#v, want %#v", got, want)
	}
	got[0] = "changed"
	if want[0] != "python3.9" {
		t.Fatal("cloneCommand returned an alias instead of an independent argv")
	}
}

func TestSelectNetworkIPAddressPrefersConfiguredNetwork(t *testing.T) {
	networks := map[string]*networktypes.EndpointSettings{
		"a-network":  {IPAddress: "172.20.0.2"},
		"yr-sandbox": {IPAddress: "172.30.0.9"},
	}

	got, err := selectNetworkIPAddress("yr-sandbox", networks)
	if err != nil {
		t.Fatalf("selectNetworkIPAddress returned error: %v", err)
	}
	if got != "172.30.0.9" {
		t.Fatalf("selected IP = %q, want configured network IP", got)
	}
}

func TestSelectNetworkIPAddressMapsDefaultToBridge(t *testing.T) {
	networks := map[string]*networktypes.EndpointSettings{
		"bridge": {IPAddress: "172.17.0.4"},
		"other":  {IPAddress: "172.19.0.4"},
	}

	got, err := selectNetworkIPAddress("default", networks)
	if err != nil {
		t.Fatalf("selectNetworkIPAddress returned error: %v", err)
	}
	if got != "172.17.0.4" {
		t.Fatalf("selected IP = %q, want bridge IP", got)
	}
}

func TestSelectNetworkIPAddressUsesDeterministicFallback(t *testing.T) {
	networks := map[string]*networktypes.EndpointSettings{
		"z-network": {IPAddress: "172.22.0.2"},
		"a-network": {IPAddress: "172.21.0.2"},
	}

	got, err := selectNetworkIPAddress("missing-network", networks)
	if err != nil {
		t.Fatalf("selectNetworkIPAddress returned error: %v", err)
	}
	if got != "172.21.0.2" {
		t.Fatalf("selected IP = %q, want sorted first network IP", got)
	}
}

func TestSelectNetworkIPAddressRejectsMissingOrInvalidAddresses(t *testing.T) {
	_, err := selectNetworkIPAddress("bridge", map[string]*networktypes.EndpointSettings{
		"bridge": {IPAddress: "not-an-ip"},
	})
	if err == nil {
		t.Fatal("selectNetworkIPAddress returned nil error for invalid endpoint")
	}
}
