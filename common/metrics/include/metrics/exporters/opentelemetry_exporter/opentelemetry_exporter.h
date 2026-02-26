#pragma once

#include "metrics/exporters/exporter.h"
#include <string>
#include <vector>
#include <map>
#include <chrono>

// OpenTelemetry headers
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter.h"

namespace observability {
namespace exporters {
namespace metrics {

struct OpenTelemetryExporterOptions {
    std::string endpoint = "http://localhost:4318/v1/metrics";
    std::string protocol = "http";
    std::chrono::milliseconds timeout = std::chrono::milliseconds(10000);
    std::map<std::string, std::string> headers;
    std::string export_mode = "BATCH";
    uint32_t batch_size = 100;
    uint32_t batch_interval = 5;
};

class OpenTelemetryExporter : public Exporter {
public:
    OpenTelemetryExporter(const std::string& config);
    OpenTelemetryExporter(const OpenTelemetryExporterOptions& options);

    ExportResult Export(const std::vector<observability::sdk::metrics::MetricData>& data) noexcept override;
    observability::sdk::metrics::AggregationTemporality GetAggregationTemporality(
        observability::sdk::metrics::InstrumentType instrumentType) const noexcept override;
    bool ForceFlush(std::chrono::microseconds) noexcept override;
    bool Shutdown(std::chrono::microseconds) noexcept override;
    void RegisterOnHealthChangeCb(const std::function<void(bool)>&) noexcept override;

private:
    OpenTelemetryExporterOptions options_;
    std::function<void(bool)> health_callback_;
    bool is_healthy_ = false;

    // OpenTelemetry-C++ exporter
    std::unique_ptr<opentelemetry::exporter::otlp::OtlpHttpMetricExporter> otlp_exporter_;
};

} // namespace metrics
} // namespace exporters
} // namespace observability
