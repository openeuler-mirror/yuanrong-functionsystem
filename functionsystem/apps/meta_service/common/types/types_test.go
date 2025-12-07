package types

import "testing"

func TestTypes(t *testing.T) {
	dr := &DispatcherRequest{}
	dr.Cancel()
	dr.IsCanceled()
}
