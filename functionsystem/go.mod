module functionsystem

go 1.21.4

require (
	github.com/agiledragon/gomonkey v2.0.1+incompatible
	github.com/smartystreets/goconvey v1.6.4
	github.com/spf13/cobra v1.8.0
	github.com/stretchr/testify v1.9.0
	go.etcd.io/etcd/client/v3 v3.5.11
)

require (
	github.com/coreos/go-semver v0.3.0 // indirect
	github.com/coreos/go-systemd/v22 v22.3.2 // indirect
	github.com/davecgh/go-spew v1.1.2-0.20180830191138-d8f796af33cc // indirect
	github.com/gogo/protobuf v1.3.2 // indirect
	github.com/golang/protobuf v1.5.4 // indirect
	github.com/google/go-cmp v0.6.0 // indirect
	github.com/gopherjs/gopherjs v0.0.0-20181017120253-0766667cb4d1 // indirect
	github.com/inconshreveable/mousetrap v1.1.0 // indirect
	github.com/jtolds/gls v4.20.0+incompatible // indirect
	github.com/pmezard/go-difflib v1.0.1-0.20181226105442-5d4384ee4fb2 // indirect
	github.com/smartystreets/assertions v0.0.0-20180927180507-b2de0cb4f26d // indirect
	github.com/spf13/pflag v1.0.5 // indirect
	go.etcd.io/etcd/api/v3 v3.5.11 // indirect
	go.etcd.io/etcd/client/pkg/v3 v3.5.11 // indirect
	go.uber.org/multierr v1.10.0 // indirect
	go.uber.org/zap v1.27.0 // indirect
	golang.org/x/net v0.26.0 // indirect
	golang.org/x/sys v0.21.0 // indirect
	golang.org/x/text v0.16.0 // indirect
	google.golang.org/genproto v0.0.0-20230822172742-b8732ec3820d // indirect
	google.golang.org/genproto/googleapis/api v0.0.0-20230822172742-b8732ec3820d // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20230822172742-b8732ec3820d // indirect
	google.golang.org/grpc v1.59.0 // indirect
	google.golang.org/protobuf v1.34.2 // indirect
	gopkg.in/yaml.v2 v2.4.0 // indirect
)

replace (
	github.com/agiledragon/gomonkey => github.com/agiledragon/gomonkey v2.0.1+incompatible
	github.com/fsnotify/fsnotify => github.com/fsnotify/fsnotify v1.7.0
	// for test or internal use
	github.com/gin-gonic/gin => github.com/gin-gonic/gin v1.10.0
	github.com/golang/mock => github.com/golang/mock v1.3.1
	github.com/google/uuid => github.com/google/uuid v1.6.0
	github.com/olekukonko/tablewriter => github.com/olekukonko/tablewriter v0.0.5
	github.com/operator-framework/operator-lib => github.com/operator-framework/operator-lib v0.4.0
	github.com/prashantv/gostub => github.com/prashantv/gostub v1.0.0
	github.com/robfig/cron/v3 => github.com/robfig/cron/v3 v3.0.1
	github.com/smartystreets/goconvey => github.com/smartystreets/goconvey v1.6.4
	github.com/spf13/cobra => github.com/spf13/cobra v1.8.0
	github.com/stretchr/testify => github.com/stretchr/testify v1.5.1
	github.com/valyala/fasthttp => github.com/valyala/fasthttp v1.55.0
	go.etcd.io/etcd/api/v3 => go.etcd.io/etcd/api/v3 v3.5.11
	go.etcd.io/etcd/client/v3 => go.etcd.io/etcd/client/v3 v3.5.11
	go.uber.org/zap => go.uber.org/zap v1.27.0
	golang.org/x/crypto => golang.org/x/crypto v0.12.0
	// affects VPC plugin building, will cause error if not pinned
	golang.org/x/net => golang.org/x/net v0.21.0
	golang.org/x/sync => golang.org/x/sync v0.0.0-20190423024810-112230192c58
	golang.org/x/sys => golang.org/x/sys v0.0.0-20210124154548-22da62e12c0c
	golang.org/x/text => golang.org/x/text v0.9.0
	golang.org/x/time => golang.org/x/time v0.3.0
	gopkg.in/yaml.v3 => gopkg.in/yaml.v3 v3.0.1
)
