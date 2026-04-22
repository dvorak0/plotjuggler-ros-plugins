#include "json_string_parser.h"

#include <QDebug>

#include <cstdint>
#include <stdexcept>

using namespace PJ;

namespace
{
uint32_t ReadLe32(const uint8_t* ptr)
{
  return (uint32_t(ptr[0]) << 0) | (uint32_t(ptr[1]) << 8) | (uint32_t(ptr[2]) << 16) |
         (uint32_t(ptr[3]) << 24);
}
}

JsonStringParser::JsonStringParser(const std::string& topic_name, PJ::PlotDataMapRef& data,
                                   PJ::MessageParserPtr fallback_parser)
  : MessageParser(topic_name, data), _fallback_parser(std::move(fallback_parser))
{
}

QString JsonStringParser::topicPrefix() const
{
  return QString::fromStdString(_topic_name);
}

bool JsonStringParser::parseRos2StringPayload(const PJ::MessageRef serialized_msg, std::string& text) const
{
  const uint8_t* data = serialized_msg.data();
  const size_t size = serialized_msg.size();

  if (size < 8)
  {
    qWarning().noquote() << QString("[%1] ROS2 String message too short to parse (%2 bytes)")
                                .arg(topicPrefix())
                                .arg(size);
    return false;
  }

  const uint32_t cdr_header = ReadLe32(data);
  if (cdr_header != 0x00010000 && cdr_header != 0x00000000 && cdr_header != 0x00000100)
  {
    qWarning().noquote() << QString("[%1] unexpected CDR encapsulation for std_msgs/String: 0x%2")
                                .arg(topicPrefix())
                                .arg(cdr_header, 8, 16, QLatin1Char('0'));
  }

  const uint32_t string_size = ReadLe32(data + 4);
  const size_t payload_end = size_t(8) + size_t(string_size);
  if (payload_end > size || string_size == 0)
  {
    qWarning().noquote() << QString("[%1] invalid std_msgs/String payload size: %2")
                                .arg(topicPrefix())
                                .arg(string_size);
    return false;
  }

  const char* str_ptr = reinterpret_cast<const char*>(data + 8);
  if (str_ptr[string_size - 1] != '\0')
  {
    qWarning().noquote() << QString("[%1] std_msgs/String payload is not null-terminated")
                                .arg(topicPrefix());
    return false;
  }

  text.assign(str_ptr, str_ptr + string_size - 1);
  return true;
}

void JsonStringParser::pushNumeric(const std::string& key, double timestamp, double value)
{
  if (key.empty())
  {
    return;
  }

  const std::string series_name = _topic_name + "/" + key;
  const QString qkey = QString::fromStdString(series_name);
  if (!_known_series.contains(qkey))
  {
    if (_known_series.size() >= qsizetype(_max_series))
    {
      qWarning().noquote() << QString("[%1] refusing to create additional JSON series beyond limit %2: %3")
                                  .arg(topicPrefix())
                                  .arg(_max_series)
                                  .arg(qkey);
      return;
    }
    _known_series.insert(qkey);
  }
  getSeries(series_name).pushBack({ timestamp, value });
}

void JsonStringParser::flattenJson(const nlohmann::json& value, const std::string& prefix,
                                   double timestamp)
{
  if (value.is_object())
  {
    for (auto it = value.begin(); it != value.end(); ++it)
    {
      const std::string child_key = prefix.empty() ? it.key() : prefix + "." + it.key();
      flattenJson(it.value(), child_key, timestamp);
    }
    return;
  }

  if (value.is_number_integer())
  {
    pushNumeric(prefix, timestamp, static_cast<double>(value.get<int64_t>()));
    return;
  }

  if (value.is_number_unsigned())
  {
    pushNumeric(prefix, timestamp, static_cast<double>(value.get<uint64_t>()));
    return;
  }

  if (value.is_number_float())
  {
    pushNumeric(prefix, timestamp, value.get<double>());
    return;
  }
}

bool JsonStringParser::parseMessage(const PJ::MessageRef serialized_msg, double& timestamp)
{
  std::string text;
  if (!parseRos2StringPayload(serialized_msg, text))
  {
    return false;
  }

  nlohmann::json value;
  try
  {
    value = nlohmann::json::parse(text);
  }
  catch (const std::exception&)
  {
    return _fallback_parser ? _fallback_parser->parseMessage(serialized_msg, timestamp) : false;
  }

  if (!value.is_object())
  {
    return _fallback_parser ? _fallback_parser->parseMessage(serialized_msg, timestamp) : false;
  }

  flattenJson(value, "", timestamp);
  return true;
}
