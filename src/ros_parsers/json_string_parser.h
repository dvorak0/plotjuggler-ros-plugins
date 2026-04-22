#pragma once

#include <PlotJuggler/messageparser_base.h>
#include <nlohmann/json.hpp>

#include <QSet>
#include <QString>

class JsonStringParser : public PJ::MessageParser
{
public:
  JsonStringParser(const std::string& topic_name, PJ::PlotDataMapRef& data,
                   PJ::MessageParserPtr fallback_parser);

  bool parseMessage(const PJ::MessageRef serialized_msg, double& timestamp) override;

private:
  bool parseRos2StringPayload(const PJ::MessageRef serialized_msg, std::string& text) const;
  void flattenJson(const nlohmann::json& value, const std::string& prefix, double timestamp);
  void pushNumeric(const std::string& key, double timestamp, double value);
  QString topicPrefix() const;

  size_t _max_series = 200;
  QSet<QString> _known_series;
  PJ::MessageParserPtr _fallback_parser;
};
