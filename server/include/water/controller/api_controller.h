#pragma once

#include "water/net/http_router.h"
#include "water/service/trace_service.h"
#include "water/service/forecast_service.h"
#include "water/service/water_service.h"
#include "water/service/classification_data_service.h"
#include "water/service/forecast_data_service.h"

#include <atomic>

namespace water::controller {

class ApiController final {
public:
    ApiController(service::WaterService& service,
                  service::TraceService& trace_service,
                  service::ForecastService& forecast_service,
                  service::ClassificationDataService* classification_service = nullptr,
                  service::ForecastDataService* forecast_data_service = nullptr);

    void registerRoutes(net::HttpRouter& router);
    void stopAccepting() noexcept { accepting_.store(false); }

private:
    void health(net::HttpRequest request, net::HttpReply reply) const;
    void companies(net::HttpRequest request, net::HttpReply reply);
    void overview(net::HttpRequest request, net::HttpReply reply);
    void classifyTrace(net::HttpRequest request, net::HttpReply reply);
    void forecastModel(net::HttpRequest request, net::HttpReply reply) const;
    void forecast(net::HttpRequest request, net::HttpReply reply);
    void classificationData(std::string operation, net::HttpRequest request,
                            net::HttpReply reply);
    void forecastData(std::string operation, net::HttpRequest request,
                      net::HttpReply reply);

    service::WaterService& service_;
    service::TraceService& trace_service_;
    service::ForecastService& forecast_service_;
    service::ClassificationDataService* classification_service_;
    service::ForecastDataService* forecast_data_service_;
    std::atomic_bool accepting_{true};
};

}  // namespace water::controller
