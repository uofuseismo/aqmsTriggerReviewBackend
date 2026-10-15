#ifndef METRICS_HPP
#define METRICS_HPP
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/exporters/otlp/otlp_http.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#ifdef WITH_OTLP_GRPC
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_options.h>
#endif
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_context.h>
#include <opentelemetry/sdk/metrics/meter_context_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/provider.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/view_factory.h>
#include "otelOptions.hpp"

namespace
{

bool metricsInitialized{false};

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    successCounter{nullptr};
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    clientErrorCounter{nullptr};
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    serverErrorCounter{nullptr};
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    unauthorizedCounter{nullptr};
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    unauthenticatedCounter{nullptr};
opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
    routePerformanceHistogram{nullptr};


void initializeRouteHistogram(
    opentelemetry::sdk::metrics::MeterProvider *providerInstance)
{
    // Histogram config
    // These three must match the instrument EXACTLY - the instrument's
    // own name and unit, and the name of the meter that created it - or
    // the view silently matches nothing and the boundaries below are
    // never applied.  See createMeters for where both are named.
    auto histogramInstrumentSelector
        = opentelemetry::sdk::metrics::InstrumentSelectorFactory::Create(
             opentelemetry::sdk::metrics::InstrumentType::kHistogram,
             "aqms.trp.route.duration",
             "s");
    auto histogramMeterSelector
        = opentelemetry::sdk::metrics::MeterSelectorFactory::Create(
             "route_duration_histogram",
             "1.2.0",
             "https://opentelemetry.io/schemas/1.2.0");
    auto histogramAggregationConfig
        = std::make_shared<opentelemetry::sdk::metrics::HistogramAggregationConfig> (); 
    // Seconds.  Strictly increasing, as the SDK requires.  Sized to where
    // the routes actually land - a few milliseconds for a freshness probe,
    // tens for waveforms, a few hundred for the catalog - with headroom
    // for a CPU-throttled pod.  Anything past the last boundary falls into
    // +Inf, and a route slower than a minute has already failed upstream.
    histogramAggregationConfig->boundaries_
        = std::vector<double> {
                               0.000,
                               0.001,
                               0.005,
                               0.010,
                               0.050,
                               0.100,
                               0.500,
                               1.000,
                               2.500,
                               5.000,
                               10.00,
                               60.00, // Hopefully user gives up now
                               };
    // The view RENAMES the metric on export, so this has to stay the
    // instrument's name.
    auto histogramView 
        = opentelemetry::sdk::metrics::ViewFactory::Create(
             "aqms.trp.route.duration",
             "Time required to for a route to succcessfully complete.",
             opentelemetry::sdk::metrics::AggregationType::kHistogram,
             histogramAggregationConfig);

    providerInstance->AddView(std::move(histogramInstrumentSelector),
                      std::move(histogramMeterSelector),
                      std::move(histogramView));

}

void initializeHTTP(
    const bool exportMetrics,
    const auto &otelHTTPMetricsOptions)
{
    if (!exportMetrics){return;}
    namespace otel = opentelemetry;
    otel::exporter::otlp::OtlpHttpMetricExporterOptions exporterOptions;
    exporterOptions.url = otelHTTPMetricsOptions.url
                        + otelHTTPMetricsOptions.suffix;
    //exporterOptions.console_debug = debug != "" && debug != "0" && debug != "no";
    exporterOptions.content_type
        = otel::exporter::otlp::HttpRequestContentType::kBinary;

    auto exporter
        = otel::exporter::otlp::OtlpHttpMetricExporterFactory::Create(
             exporterOptions);
    // Initialize and set the global MeterProvider
    otel::sdk::metrics::PeriodicExportingMetricReaderOptions readerOptions;
    readerOptions.export_interval_millis
        = otelHTTPMetricsOptions.exportInterval;
    readerOptions.export_timeout_millis
        = otelHTTPMetricsOptions.exportTimeOut;

    auto reader
        = otel::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
             std::move(exporter),
             readerOptions);

    auto context = otel::sdk::metrics::MeterContextFactory::Create();
    context->AddMetricReader(std::move(reader));

    auto metricsProvider
        = otel::sdk::metrics::MeterProviderFactory::Create(
             std::move(context));

    ::initializeRouteHistogram(metricsProvider.get());

    const std::shared_ptr<otel::metrics::MeterProvider>
        provider(std::move(metricsProvider));
    otel::sdk::metrics::Provider::SetMeterProvider(provider);
    metricsInitialized = true;
}

#ifdef WITH_OTLP_GRPC
void initializeGRPC(
    const bool exportMetrics,
    const auto &otelGRPCMetricsOptions)
{
    if (!exportMetrics){return;}
    namespace otel = opentelemetry;
    otel::exporter::otlp::OtlpGrpcMetricExporterOptions exporterOptions;
    exporterOptions.endpoint = otelGRPCMetricsOptions.url;
    exporterOptions.use_ssl_credentials = false;
    if (!otelGRPCMetricsOptions.certificatePath.empty())
    {   
        exporterOptions.use_ssl_credentials = true;
        exporterOptions.ssl_credentials_cacert_path
           = otelGRPCMetricsOptions.certificatePath;
    }   
    auto exporter
        = otel::exporter::otlp::OtlpGrpcMetricExporterFactory::Create(
             exporterOptions);

    // Initialize and set the global MeterProvider
    otel::sdk::metrics::PeriodicExportingMetricReaderOptions readerOptions;
    readerOptions.export_interval_millis
        = otelGRPCMetricsOptions.exportInterval;
    readerOptions.export_timeout_millis
        = otelGRPCMetricsOptions.exportTimeOut;

    auto reader
        = otel::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
             std::move(exporter),
             readerOptions);

    auto context = otel::sdk::metrics::MeterContextFactory::Create();
    context->AddMetricReader(std::move(reader));

    auto metricsProvider
        = otel::sdk::metrics::MeterProviderFactory::Create(
             std::move(context));

    ::initializeRouteHistogram(metricsProvider.get());

    const std::shared_ptr<otel::metrics::MeterProvider>
        provider(std::move(metricsProvider));
    otel::sdk::metrics::Provider::SetMeterProvider(provider);
    metricsInitialized = true;
}
#endif

/// Send 200s for routes
void observeSuccessCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        // N.B. these should all be noexcepts
        auto &instance
            = AQMSTriggerReviewBackend::Metrics::MetricsSingleton::getInstance();
        auto map = instance.getSuccessCounters();
        for (const auto &item : map)
        {
            auto key = item.first;
            auto value = item.second;
            std::map<std::string, std::string>
                attribute{ {"route", item.first} };
            observer->Observe(value, attribute);
        }
    }
}

/// Send 400s for routes
void observeClientErrorCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <   
            opentelemetry::nostd::shared_ptr
            <   
                opentelemetry::metrics::ObserverResultT<int64_t>
            >   
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <   
            opentelemetry::nostd::shared_ptr
            <   
               opentelemetry::metrics::ObserverResultT<int64_t>
            >   
        > (observerResult);
        // N.B. these should all be noexcepts
        auto &instance
            = AQMSTriggerReviewBackend::Metrics::MetricsSingleton::getInstance();
        auto map = instance.getClientErrorCounters();
        for (const auto &item : map)
        {
            auto key = item.first;
            auto value = item.second;
            std::map<std::string, std::string>
                attribute{ {"route", item.first} };
            observer->Observe(value, attribute);
        }
    }   
}

/// Send 500s for routes
void observeServerErrorCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <   
            opentelemetry::nostd::shared_ptr
            <   
                opentelemetry::metrics::ObserverResultT<int64_t>
            >   
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <   
            opentelemetry::nostd::shared_ptr
            <   
               opentelemetry::metrics::ObserverResultT<int64_t>
            >   
        > (observerResult);
        // N.B. these should all be noexcepts
        auto &instance
            = AQMSTriggerReviewBackend::Metrics::MetricsSingleton::getInstance();
        auto map = instance.getServerErrorCounters();
        for (const auto &item : map)
        {
            auto key = item.first;
            auto value = item.second;
            std::map<std::string, std::string>
                attribute{ {"route", item.first} };
            observer->Observe(value, attribute);
        }
    }   
}

/// 401 errors
void observeUnauthenticatedCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        // N.B. these should all be noexcepts
        auto &instance
            = AQMSTriggerReviewBackend::Metrics::MetricsSingleton::getInstance();
        observer->Observe(instance.getUnauthenticatedCounts());
    }
}

/// 403 errors
void observeUnauthorizedCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {   
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        // N.B. these should all be noexcepts
        auto &instance
            = AQMSTriggerReviewBackend::Metrics::MetricsSingleton::getInstance();
        observer->Observe(instance.getUnauthorizedCounts());
    }   
}


}

///-------------------------------------------------------------------------///

namespace AQMSTriggerReviewBackend::Metrics
{

void initialize(
    // NOLINTNEXTLINE(misc-include-cleaner)
    const ProgramOptions &options)
{
    if (options.exportMetricsWithHTTP)
    {   
        return ::initializeHTTP(options.exportMetrics,
                                options.otelHTTPMetricsOptions);
    }   
    else
    {   
#ifdef WITH_OTLP_GRPC
        return ::initializeGRPC(options.exportMetrics,
                                options.otelGRPCMetricsOptions);
#else
        throw std::runtime_error("Recompile with Conan and OTLP_GRPC");
#endif
    }   
}

void createMeters(const ProgramOptions &options)
{
    if (!options.exportMetrics){return;}

    auto provider
        = opentelemetry::metrics::Provider::GetMeterProvider();
    if (provider == nullptr)
    {
        throw std::runtime_error("Provider is null");
    }
    auto meter = provider->GetMeter(options.applicationName, "1.2.0");
    if (meter == nullptr){std::runtime_error("Meter is null");}

    // 200 counts (all routes)
    successCounter
        = meter->CreateInt64ObservableCounter(
             "aqms.trp.responses.success",
             "Number of 200 responses to user.",
             ""); 
    successCounter->AddCallback(::observeSuccessCounts, nullptr);

    // 400 counts (all routes)
    clientErrorCounter
        = meter->CreateInt64ObservableCounter(
             "aqms.trp.responses.errors.client",
             "Number of 40x responses to user.",
             ""); 
    clientErrorCounter->AddCallback(::observeClientErrorCounts, nullptr);

    // 500 counts (all routes)
    serverErrorCounter
        = meter->CreateInt64ObservableCounter(
             "aqms.trp.responses.errors.server",
             "Number of 50x responses to user.",
             "");
    serverErrorCounter->AddCallback(::observeServerErrorCounts, nullptr);

    // 401 counts
    unauthenticatedCounter
        = meter->CreateInt64ObservableCounter(
             "aqms.trp.responses.unauthenticated",
             "Number of 401 Unauthorized responses to user.",
             ""); 
    unauthenticatedCounter->AddCallback(
        ::observeUnauthenticatedCounts, nullptr);

    // 403 counts (I know who you are but you don't have permission)
    unauthorizedCounter
        = meter->CreateInt64ObservableCounter(
             "aqms.trp.responses.unauthorized",
             "Number of 403 Forbidden responses to user.",
             "");
    unauthorizedCounter->AddCallback(
        ::observeUnauthorizedCounts, nullptr);

    auto histogramMeter
        = provider->GetMeter("route_duration_histogram", "1.2.0");
    routePerformanceHistogram 
        = histogramMeter->CreateDoubleHistogram(
            "aqms.trp.route.duration",
            "Time required for a route to succcessfully complete",
            "s");

}

void recordRequestDuration(const std::string &route,
                           const std::chrono::nanoseconds &duration)
{
    if (!metricsInitialized){return;}
    auto durationSeconds = static_cast<double> (duration.count())*1.e-9;
    // TODO do i need a context auto otelContext = opentelemetry::context::Context {};
    std::map<std::string, std::string> histogramKey{ {"route", route} };
    routePerformanceHistogram->Record(durationSeconds, histogramKey);
}

void cleanup()
{
    if (metricsInitialized)
    {   
        const std::shared_ptr<opentelemetry::metrics::MeterProvider> none;
        opentelemetry::sdk::metrics::Provider::SetMeterProvider(none);
    }   
    metricsInitialized = false;
}

}
#endif


