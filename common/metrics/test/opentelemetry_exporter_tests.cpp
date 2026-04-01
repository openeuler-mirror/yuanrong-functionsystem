#include <gtest/gtest.h>
#include "metrics/exporters/opentelemetry_exporter/opentelemetry_exporter.h"

namespace observability {
namespace exporters {
namespace metrics {

TEST(OpenTelemetryExporterTest, CreateExporterWithDefaultConfig) {
    OpenTelemetryExporter exporter("{}");
    EXPECT_TRUE(true); // If we got here, the exporter was created successfully
}

TEST(OpenTelemetryExporterTest, CreateExporterWithCustomConfig) {
    std::string config = R"({
        "endpoint": "http://localhost:4318/v1/metrics",
        "protocol": "http",
        "timeout": 10000,
        "headers": {
            "Authorization": "Bearer token"
        },
        "export_mode": "BATCH",
        "batch_size": 100,
        "batch_interval": 5
    })";

    OpenTelemetryExporter exporter(config);
    EXPECT_TRUE(true); // If we got here, the exporter was created successfully
}

TEST(OpenTelemetryExporterTest, ForceFlush) {
    OpenTelemetryExporter exporter("{}");
    bool result = exporter.ForceFlush(std::chrono::microseconds(1000));
    EXPECT_TRUE(result);
}

TEST(OpenTelemetryExporterTest, Shutdown) {
    OpenTelemetryExporter exporter("{}");
    bool result = exporter.Shutdown(std::chrono::microseconds(1000));
    EXPECT_TRUE(result);
}

} // namespace metrics
} // namespace exporters
} // namespace observability
