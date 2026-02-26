#include <memory>
#include <new>

#include "metrics/exporters/opentelemetry_exporter/opentelemetry_exporter.h"
#include "metrics/plugin/exporter_handle.h"
#include "metrics/plugin/hook.h"

namespace observability::exporters::metrics {

class OtelExporterHandle final : public observability::plugin::metrics::ExporterHandle {
public:
    explicit OtelExporterHandle(std::shared_ptr<OpenTelemetryExporter> &&exporter) noexcept : exporter_(exporter)
    {
    }

    observability::exporters::metrics::Exporter &Exporter() const noexcept override
    {
        return *exporter_;
    }

private:
    std::shared_ptr<OpenTelemetryExporter> exporter_;
};

class FactoryImpl final : public observability::plugin::metrics::Factory::FactoryImpl {
public:
    std::unique_ptr<observability::plugin::metrics::ExporterHandle> MakeExporterHandle(
        std::string exporterConfig, std::unique_ptr<char[]> &) const noexcept override
    {
        try {
            auto exporter = std::make_shared<OpenTelemetryExporter>(exporterConfig);
            return std::unique_ptr<OtelExporterHandle>{ new (std::nothrow) OtelExporterHandle(std::move(exporter)) };
        } catch (...) {
            return nullptr;
        }
    }
};

static std::unique_ptr<observability::plugin::metrics::Factory::FactoryImpl> MakeFactoryImpl(
    std::unique_ptr<char[]>& /* error */) noexcept
{
    return std::unique_ptr<observability::plugin::metrics::Factory::FactoryImpl>{ new (std::nothrow) FactoryImpl{} };
}

OBSERVABILITY_DEFINE_PLUGIN_HOOK(MakeFactoryImpl);
}  // namespace observability::exporters::metrics
