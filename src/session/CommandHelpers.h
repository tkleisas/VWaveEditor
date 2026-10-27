#pragma once

#include "vwave/Audio.h"
#include "vwave/AudioIO.h"
#include "vwave/Document.h"
#include "vwave/Mastering.h"
#include "vwave/Timeline.h"
#include "vwave/Wavetable.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace vwave::session {

void setError(std::string* error, const std::string& message);
bool parseFormat(const std::string& name, AudioFormat& format);
bool parseBitDepth(const std::string& name, BitDepth& depth);
AudioExportOptions exportOptionsFrom(const nlohmann::json& params,
                                     std::string* error);
bool parseOptionalRange(const Audio& audio, const nlohmann::json& params,
                        Range& range, bool& hasRange, std::string* error);
bool prepareSelection(Timeline& timeline, const nlohmann::json& params,
                      std::string* error);
StretchAlgorithm stretchAlgorithm(const nlohmann::json& params,
                                  std::string* error);
nlohmann::json sliceToJson(const Slice& slice, int sampleRate);
nlohmann::json markerToJson(const Marker& marker, int sampleRate);
bool parseDoubleArray(const nlohmann::json& value, std::vector<double>& out);
nlohmann::json wavetableToJson(const Wavetable& table);
bool applyWavetableParams(Wavetable& table, const nlohmann::json& params,
                          std::string* error);
bool saveWavetableFile(const Wavetable& table, const std::string& path,
                       int sampleRate, std::string* error);
std::string requiredString(const nlohmann::json& params, const char* key,
                           std::string* error);
int optionalInt(const nlohmann::json& params, const char* key, int fallback);
double optionalDouble(const nlohmann::json& params, const char* key,
                      double fallback);
bool optionalBool(const nlohmann::json& params, const char* key,
                  bool fallback);
bool parseMasteringOptions(const nlohmann::json& params,
                           MasteringOptions& options, std::string* error);
nlohmann::json okResult();

} // namespace vwave::session
