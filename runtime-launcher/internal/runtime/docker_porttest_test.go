package runtime

import (
	"testing"

	"github.com/docker/go-connections/nat"
)

// parsePortMappings declares L7 schemes (http/https/ws/wss) but the docker port
// binding underneath is always TCP; legacy tcp/udp/sctp pass through.
func TestParsePortMappingsSchemes(t *testing.T) {
	cases := []struct {
		name    string
		in      string
		wantKey nat.Port // "" => expect error
	}{
		{"http maps to tcp", "http:21006:50090", "50090/tcp"},
		{"https maps to tcp", "https:8443:443", "443/tcp"},
		{"ws maps to tcp", "ws:9001:9000", "9000/tcp"},
		{"wss maps to tcp", "wss:9443:9442", "9442/tcp"},
		{"tcp passthrough", "tcp:30000:8080", "8080/tcp"},
		{"udp passthrough", "udp:30001:53", "53/udp"},
		{"invalid scheme", "ftp:1:2", ""},
		{"missing parts", "http:50090", ""},
		{"bad host port", "http:notaport:50090", ""},
		{"container port out of range", "http:21006:70000", ""},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			exposed, bindings, err := parsePortMappings([]string{c.in})
			if c.wantKey == "" {
				if err == nil {
					t.Fatalf("parsePortMappings(%q) = nil error, want error", c.in)
				}
				return
			}
			if err != nil {
				t.Fatalf("parsePortMappings(%q) error: %v", c.in, err)
			}
			if _, ok := exposed[c.wantKey]; !ok {
				t.Errorf("exposed ports = %v, want key %q", exposed, c.wantKey)
			}
			b, ok := bindings[c.wantKey]
			if !ok || len(b) != 1 {
				t.Fatalf("bindings[%q] = %v, want one binding", c.wantKey, b)
			}
		})
	}
}

// The host port must be carried through to the docker binding unchanged.
func TestParsePortMappingsHostPort(t *testing.T) {
	_, bindings, err := parsePortMappings([]string{"http:21006:50090"})
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got := bindings["50090/tcp"][0].HostPort; got != "21006" {
		t.Errorf("hostPort = %q, want 21006", got)
	}
}
