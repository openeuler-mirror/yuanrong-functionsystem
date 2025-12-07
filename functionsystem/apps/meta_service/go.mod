module meta_service

go 1.24.1

require (
	github.com/agiledragon/gomonkey v2.0.1+incompatible
	github.com/agiledragon/gomonkey/v2 v2.13.0
	github.com/asaskevich/govalidator/v11 v11.0.1-0.20250122183457-e11347878e23
	github.com/fsnotify/fsnotify v0.0.0-00010101000000-000000000000
	github.com/gin-gonic/gin v0.0.0-00010101000000-000000000000
	github.com/golang/mock v0.0.0-00010101000000-000000000000
	github.com/huaweicloud/huaweicloud-sdk-go-obs v3.25.9+incompatible
	github.com/magiconair/properties v1.8.10
	github.com/pborman/uuid v1.2.1
	github.com/redis/go-redis/v9 v9.17.2
	github.com/smartystreets/assertions v0.0.0-20180927180507-b2de0cb4f26d
	github.com/smartystreets/goconvey v1.6.4
	github.com/stretchr/testify v1.11.1
	github.com/valyala/fasthttp v0.0.0-00010101000000-000000000000
	go.etcd.io/etcd/api/v3 v3.5.11
	go.etcd.io/etcd/client/v3 v3.5.11
	go.opentelemetry.io/otel v1.38.0
	go.opentelemetry.io/otel/exporters/otlp/otlptrace v1.38.0
	go.opentelemetry.io/otel/exporters/otlp/otlptrace/otlptracegrpc v1.38.0
	go.opentelemetry.io/otel/sdk v1.38.0
	go.opentelemetry.io/otel/trace v1.38.0
	go.uber.org/zap v1.27.0
	golang.org/x/crypto v0.39.0
	golang.org/x/net v0.43.0
	golang.org/x/text v0.28.0
	golang.org/x/time v0.9.0
	google.golang.org/grpc v1.75.0
	gopkg.in/yaml.v3 v3.0.1
	gotest.tools v2.2.0+incompatible
	k8s.io/api v0.34.2
	k8s.io/apimachinery v0.34.2
	k8s.io/client-go v0.34.2
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
