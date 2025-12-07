package service

import (
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/assertions/should"
	. "github.com/smartystreets/goconvey/convey"

	"meta_service/function_repo/model"
	"meta_service/function_repo/storage"
	"functionsystem/pkg/meta_service/server"
	"functionsystem/pkg/meta_service/test/fakecontext"
)

func Test_createReserveInstance(t *testing.T) {

	Convey("Test Create Reserve Instance ClusterID not exist", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetFunctionByFunctionNameAndVersion, func(ctx server.Context, name string,
			version string, kind string) (storage.FunctionVersionValue, error) {
			funcVer := storage.FunctionVersionValue{}
			return funcVer, nil
		})
		defer patches.Reset()
		ctx := fakecontext.NewMockContext()
		req := model.CreateReserveInsRequest{}
		req.FuncName = ""
		req.Version = "latest"
		req.TenantID = "0"
		req.InstanceLabel = "label001"
		req.InstanceConfigInfos = []model.InstanceConfig{
			{
				ClusterID:   "cluster-001",
				MinInstance: 1,
				MaxInstance: 100,
			},
		}
		_, err := CreateReserveInstance(ctx, req, false)
		ShouldNotBeNil(err)
		So(err.Error(), should.ContainSubstring, "clusterID cluster-001 is not found")
	})
}
