#include <grapple/app/NativeStewardPlanner.hpp>

#include <grapple/effects/BuiltinEffects.hpp>
#include <grapple/graph/GraphNode.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace grapple::app {

namespace {

constexpr double CenteredCameraTransformPositionX = 0.0;
constexpr double CenteredCameraTransformPositionY = 0.0;
constexpr double NormalCameraTransformZoom = 1.0;
constexpr double CameraTransformPositionXStep = 0.25;
constexpr double CameraTransformPositionYStep = 0.2;
constexpr double CameraTransformZoomInStep = 0.25;
constexpr double CameraTransformZoomOutStep = 0.2;
constexpr double ClipRotationStepDegrees = 15.0;
constexpr double ClipTimingStepSeconds = 1.0;
constexpr double TitleTextPositionY = 0.35;
constexpr double LowerThirdTextPositionY = -0.35;
constexpr double TitleTextFontSize = 64.0;
constexpr double LowerThirdTextFontSize = 44.0;
constexpr std::string_view DefaultNoteTitle{"Project Note"};

double intentStrengthMultiplier(const std::string& normalized);

std::string lowercaseAscii(std::string value) {
  for (char& character : value) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

bool containsText(const std::string& value, std::string_view text) {
  return value.find(text) != std::string::npos;
}

bool containsAsciiWord(const std::string& value, std::string_view word) {
  std::size_t position = value.find(word);
  while (position != std::string::npos) {
    const bool leftBoundary =
      position == 0 ||
      std::isalnum(static_cast<unsigned char>(value[position - 1])) == 0;
    const std::size_t right = position + word.size();
    const bool rightBoundary =
      right >= value.size() ||
      std::isalnum(static_cast<unsigned char>(value[right])) == 0;
    if (leftBoundary && rightBoundary) {
      return true;
    }
    position = value.find(word, position + 1);
  }
  return false;
}

bool startsWithText(std::string_view value, std::string_view text) {
  return value.substr(0, text.size()) == text;
}

std::string_view trimLeft(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  return value;
}

std::string trimCopy(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return std::string{value};
}

std::string stripLeadingTextSeparators(std::string value) {
  value = trimCopy(value);
  while (!value.empty() && (value.front() == ':' || value.front() == '-' || value.front() == '"' || value.front() == '\'')) {
    value.erase(value.begin());
    value = trimCopy(value);
  }
  while (!value.empty() && (value.back() == '"' || value.back() == '\'' || value.back() == '.')) {
    value.pop_back();
    value = trimCopy(value);
  }
  return value;
}

bool startsNewTransformOperation(std::string_view value) {
  const std::string_view trimmed = trimLeft(value);
  for (std::string_view action : {
         std::string_view{"rotate"},
         std::string_view{"rotation"},
         std::string_view{"tilt"},
         std::string_view{"tilted"},
         std::string_view{"straighten"},
         std::string_view{"scale"},
         std::string_view{"shrink"},
         std::string_view{"make"},
         std::string_view{"hide"},
         std::string_view{"fade"}
       }) {
    if (startsWithText(trimmed, action)) {
      return true;
    }
  }
  return false;
}

std::string actionClause(
  const std::string& normalized,
  std::initializer_list<std::string_view> actionWords
) {
  std::size_t actionPosition = std::string::npos;
  for (std::string_view actionWord : actionWords) {
    const std::size_t position = normalized.find(actionWord);
    if (position != std::string::npos && (actionPosition == std::string::npos || position < actionPosition)) {
      actionPosition = position;
    }
  }
  if (actionPosition == std::string::npos) {
    return normalized;
  }

  std::size_t endPosition = normalized.size();
  for (std::string_view separator : {std::string_view{","}, std::string_view{"."}}) {
    const std::size_t separatorPosition = normalized.find(separator, actionPosition + 1);
    if (separatorPosition != std::string::npos && separatorPosition < endPosition) {
      endPosition = separatorPosition;
    }
  }
  std::size_t andPosition = normalized.find(" and ", actionPosition + 1);
  while (andPosition != std::string::npos) {
    if (andPosition < endPosition && startsNewTransformOperation(std::string_view{normalized}.substr(andPosition + 5))) {
      endPosition = andPosition;
      break;
    }
    andPosition = normalized.find(" and ", andPosition + 5);
  }
  return normalized.substr(actionPosition, endPosition - actionPosition);
}

bool cameraIntentRequestsZoomOut(const std::string& normalized) {
  return containsText(normalized, "zoom out") ||
         containsAsciiWord(normalized, "wide") ||
         containsAsciiWord(normalized, "wider") ||
         containsAsciiWord(normalized, "smaller") ||
         containsAsciiWord(normalized, "shrink");
}

bool cameraIntentRequestsZoomIn(const std::string& normalized) {
  return containsText(normalized, "zoom in") ||
         containsAsciiWord(normalized, "closer") ||
         containsAsciiWord(normalized, "close") ||
         containsAsciiWord(normalized, "larger") ||
         containsAsciiWord(normalized, "bigger");
}

bool cameraIntentRequestsTemporalMotion(const std::string& normalized) {
  return containsAsciiWord(normalized, "pan") ||
         containsText(normalized, "animate") ||
         containsText(normalized, "over time") ||
         containsText(normalized, "gradual") ||
         containsText(normalized, "slowly");
}

bool cameraIntentRequestsCenter(const std::string& normalized) {
  return containsAsciiWord(normalized, "center") ||
         containsAsciiWord(normalized, "centre") ||
         containsAsciiWord(normalized, "recenter") ||
         containsAsciiWord(normalized, "recentre");
}

bool cameraIntentRequestsReset(const std::string& normalized) {
  return containsAsciiWord(normalized, "reset");
}

bool undoIntentRequestsLastEdit(const std::string& normalized) {
  if (containsAsciiWord(normalized, "undo")) {
    return true;
  }
  if (!containsAsciiWord(normalized, "revert")) {
    return false;
  }
  return containsText(normalized, "last") ||
         containsText(normalized, "previous") ||
         containsText(normalized, "change") ||
         containsText(normalized, "edit");
}

bool redoIntentRequestsLastUndoneEdit(const std::string& normalized) {
  return containsAsciiWord(normalized, "redo");
}

bool cameraTransformDeleteIntentRequestsControls(const std::string& normalized) {
  const bool deleteRequested =
    containsAsciiWord(normalized, "delete") ||
    containsAsciiWord(normalized, "remove");
  if (!deleteRequested) {
    return false;
  }
  return containsAsciiWord(normalized, "camera") ||
         containsAsciiWord(normalized, "control") ||
         containsAsciiWord(normalized, "controls") ||
         containsAsciiWord(normalized, "effect") ||
         containsAsciiWord(normalized, "framing");
}

bool cameraUpdateIntentRequestsCamera(const std::string& normalized) {
  if (!containsAsciiWord(normalized, "camera")) {
    return false;
  }
  return containsAsciiWord(normalized, "rename") ||
         containsAsciiWord(normalized, "name") ||
         containsText(normalized, "focal length") ||
         containsAsciiWord(normalized, "focal") ||
         containsAsciiWord(normalized, "lens");
}

bool trackDeleteIntentRequestsTrack(const std::string& normalized) {
  const bool deleteRequested =
    containsAsciiWord(normalized, "delete") ||
    containsAsciiWord(normalized, "remove");
  if (!deleteRequested) {
    return false;
  }
  return containsAsciiWord(normalized, "track") ||
         containsAsciiWord(normalized, "layer");
}

bool trackCreateIntentRequestsTrack(const std::string& normalized) {
  const bool createRequested =
    containsAsciiWord(normalized, "add") ||
    containsAsciiWord(normalized, "create") ||
    containsAsciiWord(normalized, "new");
  if (!createRequested || trackDeleteIntentRequestsTrack(normalized)) {
    return false;
  }
  return containsAsciiWord(normalized, "track") ||
         containsAsciiWord(normalized, "layer");
}

timeline::TrackKind trackKindForIntent(const std::string& normalized) {
  if (containsAsciiWord(normalized, "audio") ||
      containsAsciiWord(normalized, "sound") ||
      containsAsciiWord(normalized, "music") ||
      containsAsciiWord(normalized, "voiceover")) {
    return timeline::TrackKind::Audio;
  }
  return timeline::TrackKind::Visual;
}

bool textIntentRequestsText(const std::string& normalized) {
  return containsAsciiWord(normalized, "title") ||
         containsAsciiWord(normalized, "text") ||
         containsAsciiWord(normalized, "caption") ||
         containsText(normalized, "lower third") ||
         containsAsciiWord(normalized, "label");
}

bool textIntentRequestsLowerThird(const std::string& normalized) {
  return containsText(normalized, "lower third") ||
         containsAsciiWord(normalized, "caption");
}

bool textClipIntentRequestsFontSize(const std::string& normalized) {
  return containsAsciiWord(normalized, "font") ||
         containsAsciiWord(normalized, "text") ||
         containsAsciiWord(normalized, "title") ||
         containsAsciiWord(normalized, "caption");
}

bool textClipIntentRequestsOpacity(const std::string& normalized) {
  return containsAsciiWord(normalized, "opacity") ||
         containsAsciiWord(normalized, "transparent") ||
         containsAsciiWord(normalized, "fade") ||
         containsAsciiWord(normalized, "opaque") ||
         containsAsciiWord(normalized, "hidden") ||
         containsAsciiWord(normalized, "hide") ||
         containsAsciiWord(normalized, "visible");
}

bool noteIntentRequestsNote(const std::string& normalized) {
  return containsAsciiWord(normalized, "note") ||
         containsAsciiWord(normalized, "notes") ||
         containsAsciiWord(normalized, "rationale") ||
         containsAsciiWord(normalized, "reminder");
}

std::optional<std::string> quotedTextFromIntent(const std::string& intent) {
  for (char quote : {'"', '\''}) {
    const std::size_t start = intent.find(quote);
    if (start == std::string::npos) {
      continue;
    }
    const std::size_t end = intent.find(quote, start + 1);
    if (end == std::string::npos || end == start + 1) {
      continue;
    }
    return intent.substr(start + 1, end - start - 1);
  }
  return std::nullopt;
}

std::string unquotedTextFromIntent(const std::string& intent, const std::string& normalized) {
  for (std::string_view marker : {
         std::string_view{"that says"},
         std::string_view{"saying"},
         std::string_view{"called"},
         std::string_view{"title"},
         std::string_view{"caption"},
         std::string_view{"lower third"},
         std::string_view{"text"},
         std::string_view{"label"}
       }) {
    const std::size_t markerPosition = normalized.find(marker);
    if (markerPosition == std::string::npos) {
      continue;
    }
    const std::size_t textStart = markerPosition + marker.size();
    if (textStart < intent.size()) {
      const std::string text = stripLeadingTextSeparators(intent.substr(textStart));
      if (!text.empty()) {
        return text;
      }
    }
  }
  return "Title";
}

std::string unquotedNoteTextFromIntent(const std::string& intent, const std::string& normalized) {
  for (std::string_view marker : {
         std::string_view{"that says"},
         std::string_view{"saying"},
         std::string_view{"body"},
         std::string_view{"markdown"},
         std::string_view{"note"},
         std::string_view{":"}
       }) {
    const std::size_t markerPosition = normalized.find(marker);
    if (markerPosition == std::string::npos) {
      continue;
    }
    const std::size_t textStart = markerPosition + marker.size();
    if (textStart < intent.size()) {
      const std::string text = stripLeadingTextSeparators(intent.substr(textStart));
      if (!text.empty()) {
        return text;
      }
    }
  }
  return stripLeadingTextSeparators(intent);
}

std::optional<std::string> noteTitleEditFromIntent(const std::string& intent, const std::string& normalized) {
  if (!containsAsciiWord(normalized, "title") &&
      !containsAsciiWord(normalized, "rename")) {
    return std::nullopt;
  }
  if (auto quoted = quotedTextFromIntent(intent)) {
    return quoted.value();
  }
  for (std::string_view marker : {
         std::string_view{"change title to"},
         std::string_view{"set title to"},
         std::string_view{"rename note to"},
         std::string_view{"title"}
       }) {
    const std::size_t markerPosition = normalized.find(marker);
    if (markerPosition == std::string::npos) {
      continue;
    }
    const std::size_t textStart = markerPosition + marker.size();
    if (textStart < intent.size()) {
      const std::string title = stripLeadingTextSeparators(intent.substr(textStart));
      if (!title.empty()) {
        return title;
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> noteMarkdownEditFromIntent(const std::string& intent, const std::string& normalized) {
  if (containsAsciiWord(normalized, "title") ||
      containsAsciiWord(normalized, "rename")) {
    return std::nullopt;
  }
  if (auto quoted = quotedTextFromIntent(intent)) {
    return quoted.value();
  }
  for (std::string_view marker : {
         std::string_view{"change body to"},
         std::string_view{"set body to"},
         std::string_view{"change markdown to"},
         std::string_view{"set markdown to"},
         std::string_view{"body"},
         std::string_view{"markdown"},
         std::string_view{"that says"},
         std::string_view{"saying"},
         std::string_view{"to"}
       }) {
    const std::size_t markerPosition = normalized.find(marker);
    if (markerPosition == std::string::npos) {
      continue;
    }
    const std::size_t textStart = markerPosition + marker.size();
    if (textStart < intent.size()) {
      const std::string markdown = stripLeadingTextSeparators(intent.substr(textStart));
      if (!markdown.empty()) {
        return markdown;
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> editedTextFromIntent(const std::string& intent, const std::string& normalized) {
  if (auto quoted = quotedTextFromIntent(intent)) {
    return quoted.value();
  }
  for (std::string_view marker : {
         std::string_view{"change text to"},
         std::string_view{"set text to"},
         std::string_view{"change title to"},
         std::string_view{"set title to"},
         std::string_view{"say"},
         std::string_view{"says"},
         std::string_view{"to"}
       }) {
    const std::size_t markerPosition = normalized.find(marker);
    if (markerPosition == std::string::npos) {
      continue;
    }
    const std::size_t textStart = markerPosition + marker.size();
    if (textStart < intent.size()) {
      const std::string text = stripLeadingTextSeparators(intent.substr(textStart));
      if (!text.empty()) {
        return text;
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> cameraNameEditFromIntent(const std::string& intent, const std::string& normalized) {
  if (!containsAsciiWord(normalized, "rename") && !containsAsciiWord(normalized, "name")) {
    return std::nullopt;
  }
  if (auto quoted = quotedTextFromIntent(intent)) {
    return quoted.value();
  }
  for (std::string_view marker : {
         std::string_view{"rename selected camera to"},
         std::string_view{"rename camera to"},
         std::string_view{"set camera name to"},
         std::string_view{"change camera name to"},
         std::string_view{"name camera"}
       }) {
    const std::size_t markerPosition = normalized.find(marker);
    if (markerPosition == std::string::npos) {
      continue;
    }
    const std::size_t textStart = markerPosition + marker.size();
    if (textStart < intent.size()) {
      const std::string name = stripLeadingTextSeparators(intent.substr(textStart));
      if (!name.empty()) {
        return name;
      }
    }
  }
  return std::nullopt;
}

std::optional<double> firstNumberFromIntent(const std::string& intent) {
  for (std::size_t index = 0; index < intent.size(); ++index) {
    const unsigned char character = static_cast<unsigned char>(intent[index]);
    if (std::isdigit(character) == 0 && intent[index] != '-' && intent[index] != '.') {
      continue;
    }
    char* end = nullptr;
    const double value = std::strtod(intent.c_str() + index, &end);
    if (end != intent.c_str() + index) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<double> focalLengthFromIntent(const std::string& intent, const std::string& normalized) {
  std::size_t markerPosition = std::string::npos;
  for (std::string_view marker : {
         std::string_view{"focal length"},
         std::string_view{"focal"},
         std::string_view{"lens"}
       }) {
    const std::size_t current = normalized.find(marker);
    if (current != std::string::npos && (markerPosition == std::string::npos || current < markerPosition)) {
      markerPosition = current + marker.size();
    }
  }
  if (markerPosition == std::string::npos || markerPosition >= intent.size()) {
    return std::nullopt;
  }
  return firstNumberFromIntent(intent.substr(markerPosition));
}

bool cameraIntentRequestsFocalLength(const std::string& normalized) {
  return containsText(normalized, "focal length") ||
         containsAsciiWord(normalized, "focal") ||
         containsAsciiWord(normalized, "lens");
}

bool clipIntentRequestsRotation(const std::string& normalized) {
  return containsAsciiWord(normalized, "rotate") ||
         containsAsciiWord(normalized, "rotation") ||
         containsAsciiWord(normalized, "tilt") ||
         containsAsciiWord(normalized, "tilted") ||
         containsAsciiWord(normalized, "straighten");
}

bool clipIntentRequestsScale(const std::string& normalized) {
  return containsText(normalized, "scale down") ||
         containsText(normalized, "scale up") ||
         containsAsciiWord(normalized, "smaller") ||
         containsAsciiWord(normalized, "shrink") ||
         containsAsciiWord(normalized, "larger") ||
         containsAsciiWord(normalized, "bigger");
}

bool clipIntentRequestsHidden(const std::string& normalized) {
  return containsAsciiWord(normalized, "invisible") ||
         containsAsciiWord(normalized, "hide") ||
         containsAsciiWord(normalized, "hidden");
}

bool clipIntentRequestsOpaque(const std::string& normalized) {
  return containsAsciiWord(normalized, "opaque") ||
         containsText(normalized, "full opacity") ||
         containsText(normalized, "fully visible") ||
         containsAsciiWord(normalized, "visible");
}

bool clipIntentRequestsOpacity(const std::string& normalized) {
  return clipIntentRequestsHidden(normalized) ||
         clipIntentRequestsOpaque(normalized) ||
         containsAsciiWord(normalized, "fade") ||
         containsAsciiWord(normalized, "transparent") ||
         containsText(normalized, "half opacity");
}

bool clipIntentRequestsPlaybackRate(const std::string& normalized) {
  return containsText(normalized, "speed up") ||
         containsText(normalized, "slow down") ||
         containsText(normalized, "normal speed") ||
         containsText(normalized, "regular speed") ||
         containsText(normalized, "half speed") ||
         containsText(normalized, "double speed") ||
         containsAsciiWord(normalized, "faster") ||
         containsAsciiWord(normalized, "slower");
}

bool clipIntentRequestsMovement(const std::string& normalized) {
  if (containsAsciiWord(normalized, "move") ||
      containsAsciiWord(normalized, "shift") ||
      containsAsciiWord(normalized, "nudge") ||
      containsAsciiWord(normalized, "slide") ||
      containsAsciiWord(normalized, "position")) {
    return true;
  }
  if (clipIntentRequestsRotation(normalized) ||
      clipIntentRequestsScale(normalized) ||
      clipIntentRequestsOpacity(normalized) ||
      clipIntentRequestsPlaybackRate(normalized)) {
    return false;
  }
  return containsAsciiWord(normalized, "left") ||
         containsAsciiWord(normalized, "right") ||
         containsAsciiWord(normalized, "up") ||
         containsAsciiWord(normalized, "down");
}

std::string clipMovementClause(const std::string& normalized) {
  if (!clipIntentRequestsMovement(normalized)) {
    return {};
  }
  return actionClause(normalized, {"move", "shift", "nudge", "slide", "position"});
}

bool clipIntentHasMovementDirection(const std::string& normalized) {
  const std::string movementClause = clipMovementClause(normalized);
  return clipIntentRequestsMovement(normalized) &&
         (containsAsciiWord(movementClause, "left") ||
          containsAsciiWord(movementClause, "right") ||
          containsAsciiWord(movementClause, "up") ||
          containsAsciiWord(movementClause, "down"));
}

bool clipIntentMentionsClipTarget(const std::string& normalized) {
  return containsAsciiWord(normalized, "clip") ||
         containsAsciiWord(normalized, "video") ||
         containsAsciiWord(normalized, "layer");
}

bool clipDeleteIntentRequestsClip(const std::string& normalized) {
  const bool deleteRequested =
    containsAsciiWord(normalized, "delete") ||
    containsAsciiWord(normalized, "remove");
  if (!deleteRequested) {
    return false;
  }
  return containsAsciiWord(normalized, "clip") ||
         containsAsciiWord(normalized, "video") ||
         containsAsciiWord(normalized, "text") ||
         containsAsciiWord(normalized, "title") ||
         containsAsciiWord(normalized, "caption");
}

bool clipTintIntentRequestsClip(const std::string& normalized) {
  if (!clipIntentMentionsClipTarget(normalized)) {
    return false;
  }
  return containsAsciiWord(normalized, "tint") ||
         containsAsciiWord(normalized, "color") ||
         containsAsciiWord(normalized, "colour") ||
         containsAsciiWord(normalized, "warmer") ||
         containsAsciiWord(normalized, "cooler") ||
         containsAsciiWord(normalized, "red") ||
         containsAsciiWord(normalized, "blue") ||
         containsAsciiWord(normalized, "green");
}

bool clipExposureIntentRequestsClip(const std::string& normalized) {
  if (!clipIntentMentionsClipTarget(normalized)) {
    return false;
  }
  return containsAsciiWord(normalized, "exposure") ||
         containsAsciiWord(normalized, "expose") ||
         containsAsciiWord(normalized, "bright") ||
         containsAsciiWord(normalized, "brighter") ||
         containsAsciiWord(normalized, "brighten") ||
         containsAsciiWord(normalized, "dark") ||
         containsAsciiWord(normalized, "darker") ||
         containsAsciiWord(normalized, "darken");
}

foundation::Vec3 clipTintColorForIntent(const std::string& normalized) {
  if (containsAsciiWord(normalized, "blue") || containsAsciiWord(normalized, "cooler")) {
    return foundation::Vec3{0.2, 0.45, 1.0};
  }
  if (containsAsciiWord(normalized, "green")) {
    return foundation::Vec3{0.2, 1.0, 0.35};
  }
  if (containsAsciiWord(normalized, "warm") || containsAsciiWord(normalized, "warmer")) {
    return foundation::Vec3{1.0, 0.55, 0.25};
  }
  return foundation::Vec3{1.0, 0.2, 0.15};
}

double clipTintAmountForIntent(const std::string& normalized) {
  if (containsAsciiWord(normalized, "subtle") ||
      containsAsciiWord(normalized, "slight") ||
      containsText(normalized, "a little")) {
    return 0.2;
  }
  if (containsAsciiWord(normalized, "strong") ||
      containsAsciiWord(normalized, "stronger") ||
      containsAsciiWord(normalized, "heavy") ||
      containsText(normalized, "a lot")) {
    return 0.6;
  }
  return 0.35;
}

double clipExposureForIntent(const std::string& normalized) {
  if (containsAsciiWord(normalized, "reset") ||
      containsAsciiWord(normalized, "normal") ||
      containsAsciiWord(normalized, "neutral")) {
    return 0.0;
  }
  const double magnitude = intentStrengthMultiplier(normalized) * 0.35;
  if (containsAsciiWord(normalized, "dark") ||
      containsAsciiWord(normalized, "darker") ||
      containsAsciiWord(normalized, "darken")) {
    return -magnitude;
  }
  return magnitude;
}

std::optional<timeline::ParamValue> effectParamValue(
  const timeline::EffectPayload& payload,
  const std::string& paramName
) {
  for (const timeline::Param& param : payload.params.values) {
    if (param.name == paramName) {
      return param.value;
    }
  }
  return std::nullopt;
}

bool clipTintIntentRequestsColor(const std::string& normalized) {
  return containsAsciiWord(normalized, "red") ||
         containsAsciiWord(normalized, "blue") ||
         containsAsciiWord(normalized, "green") ||
         containsAsciiWord(normalized, "warm") ||
         containsAsciiWord(normalized, "warmer") ||
         containsAsciiWord(normalized, "cool") ||
         containsAsciiWord(normalized, "cooler");
}

bool clipTintIntentRequestsAmount(const std::string& normalized) {
  return containsAsciiWord(normalized, "subtle") ||
         containsAsciiWord(normalized, "slight") ||
         containsText(normalized, "a little") ||
         containsAsciiWord(normalized, "strong") ||
         containsAsciiWord(normalized, "stronger") ||
         containsAsciiWord(normalized, "heavy") ||
         containsText(normalized, "a lot") ||
         containsAsciiWord(normalized, "amount") ||
         containsAsciiWord(normalized, "intensity");
}

bool clipExposureIntentRequestsAmount(const std::string& normalized) {
  return containsAsciiWord(normalized, "exposure") ||
         containsAsciiWord(normalized, "expose") ||
         containsAsciiWord(normalized, "bright") ||
         containsAsciiWord(normalized, "brighter") ||
         containsAsciiWord(normalized, "brighten") ||
         containsAsciiWord(normalized, "dark") ||
         containsAsciiWord(normalized, "darker") ||
         containsAsciiWord(normalized, "darken") ||
         containsAsciiWord(normalized, "reset") ||
         containsAsciiWord(normalized, "normal") ||
         containsAsciiWord(normalized, "neutral");
}

bool clipIntentRequestsMoveLater(const std::string& normalized) {
  return containsAsciiWord(normalized, "later") ||
         containsText(normalized, "move forward");
}

bool clipIntentRequestsMoveEarlier(const std::string& normalized) {
  return containsAsciiWord(normalized, "earlier") ||
         containsText(normalized, "move backward") ||
         containsText(normalized, "move back");
}

bool clipIntentRequestsTrimShorter(const std::string& normalized) {
  return containsAsciiWord(normalized, "shorter") ||
         containsAsciiWord(normalized, "shorten") ||
         containsText(normalized, "trim end") ||
         containsText(normalized, "trim shorter");
}

bool clipIntentRequestsTrimLonger(const std::string& normalized) {
  return containsAsciiWord(normalized, "longer") ||
         containsAsciiWord(normalized, "extend");
}

double intentStrengthMultiplier(const std::string& normalized) {
  if (containsText(normalized, "a little") ||
      containsAsciiWord(normalized, "slight") ||
      containsAsciiWord(normalized, "slightly") ||
      containsAsciiWord(normalized, "subtle") ||
      containsAsciiWord(normalized, "gently")) {
    return 0.5;
  }
  if (containsText(normalized, "a lot") ||
      containsAsciiWord(normalized, "much") ||
      containsAsciiWord(normalized, "far") ||
      containsAsciiWord(normalized, "dramatic") ||
      containsAsciiWord(normalized, "dramatically")) {
    return 2.0;
  }
  return 1.0;
}

int clipTransformOperationCount(const std::string& normalized) {
  int count = 0;
  if (clipIntentHasMovementDirection(normalized)) {
    ++count;
  }
  if (clipIntentRequestsScale(normalized)) {
    ++count;
  }
  if (clipIntentRequestsRotation(normalized)) {
    ++count;
  }
  if (clipIntentRequestsOpacity(normalized)) {
    ++count;
  }
  return count;
}

double clipMovementStrengthMultiplier(
  const std::string& normalized,
  const std::string& movementClause,
  bool mixedTransformRequest
) {
  if (!mixedTransformRequest) {
    return intentStrengthMultiplier(normalized);
  }
  if (containsText(movementClause, "slightly move") ||
      containsText(movementClause, "move slightly") ||
      containsText(movementClause, "a little move") ||
      containsAsciiWord(movementClause, "nudge")) {
    return 0.5;
  }
  if (containsText(movementClause, "move far") ||
      containsText(movementClause, "far left") ||
      containsText(movementClause, "far right") ||
      containsText(movementClause, "far up") ||
      containsText(movementClause, "far down") ||
      containsText(movementClause, "move a lot") ||
      containsText(movementClause, "move much")) {
    return 2.0;
  }
  return 1.0;
}

double clipScaleStrengthMultiplier(const std::string& normalized, bool mixedTransformRequest) {
  if (!mixedTransformRequest) {
    return intentStrengthMultiplier(normalized);
  }
  if (containsText(normalized, "slightly smaller") ||
      containsText(normalized, "slightly bigger") ||
      containsText(normalized, "slightly larger") ||
      containsText(normalized, "a little smaller") ||
      containsText(normalized, "a little bigger") ||
      containsText(normalized, "a little larger")) {
    return 0.5;
  }
  if (containsText(normalized, "much smaller") ||
      containsText(normalized, "much bigger") ||
      containsText(normalized, "much larger") ||
      containsText(normalized, "a lot smaller") ||
      containsText(normalized, "a lot bigger") ||
      containsText(normalized, "a lot larger")) {
    return 2.0;
  }
  return 1.0;
}

double clipRotationStrengthMultiplier(const std::string& normalized, bool mixedTransformRequest) {
  if (!mixedTransformRequest) {
    return intentStrengthMultiplier(normalized);
  }
  if (containsText(normalized, "rotate slightly") ||
      containsText(normalized, "slightly rotate") ||
      containsText(normalized, "tilt slightly") ||
      containsText(normalized, "slightly tilt") ||
      containsText(normalized, "slightly left") ||
      containsText(normalized, "slightly right")) {
    return 0.5;
  }
  if (containsText(normalized, "rotate far") ||
      containsText(normalized, "rotate a lot") ||
      containsText(normalized, "rotate much") ||
      containsText(normalized, "tilt far") ||
      containsText(normalized, "tilt a lot") ||
      containsText(normalized, "tilt much")) {
    return 2.0;
  }
  return 1.0;
}

foundation::Result<double> numericEffectParamValue(
  const timeline::EffectPayload& payload,
  const std::string& paramName
) {
  const auto param = std::find_if(payload.params.values.begin(), payload.params.values.end(), [&](const timeline::Param& value) {
    return value.name == paramName;
  });
  if (param == payload.params.values.end()) {
    return foundation::Error{
      "steward.camera_transform_param_missing",
      "Camera Transform controls are missing the requested parameter."
    };
  }
  const auto* value = std::get_if<double>(&param->value);
  if (value == nullptr) {
    return foundation::Error{
      "steward.camera_transform_param_not_numeric",
      "Camera Transform control parameter must be numeric."
    };
  }
  return *value;
}

double applyCameraTransformOperation(
  double currentValue,
  CameraTransformAdjustmentOperation operation,
  double operand
) {
  switch (operation) {
    case CameraTransformAdjustmentOperation::Set:
      return operand;
    case CameraTransformAdjustmentOperation::Add:
      return currentValue + operand;
    case CameraTransformAdjustmentOperation::Multiply:
      return currentValue * operand;
  }

  return currentValue;
}

foundation::Result<std::optional<CameraTransformParamAdjustment>> cameraTransformParamAdjustment(
  const timeline::EffectPayload& payload,
  const foundation::NodeId& effectNodeId,
  std::string paramName,
  CameraTransformAdjustmentOperation operation,
  double operand
) {
  auto currentValue = numericEffectParamValue(payload, paramName);
  if (!currentValue) {
    return currentValue.error();
  }

  const double value = applyCameraTransformOperation(currentValue.value(), operation, operand);
  if (value == currentValue.value()) {
    return std::optional<CameraTransformParamAdjustment>{};
  }

  return std::optional<CameraTransformParamAdjustment>{CameraTransformParamAdjustment{
    effectNodeId,
    std::move(paramName),
    value,
    operand,
    operation
  }};
}

} // namespace

CameraTransformIntentDefaults NativeStewardPlanner::cameraTransformDefaultsForIntent(
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  const double strength = intentStrengthMultiplier(normalized);
  CameraTransformIntentDefaults defaults;

  if (containsAsciiWord(normalized, "left")) {
    defaults.positionX = -CameraTransformPositionXStep * strength;
  } else if (containsAsciiWord(normalized, "right")) {
    defaults.positionX = CameraTransformPositionXStep * strength;
  }

  if (containsAsciiWord(normalized, "up")) {
    defaults.positionY = -CameraTransformPositionYStep * strength;
  } else if (containsAsciiWord(normalized, "down")) {
    defaults.positionY = CameraTransformPositionYStep * strength;
  }

  if (cameraIntentRequestsZoomOut(normalized)) {
    defaults.zoom = NormalCameraTransformZoom - CameraTransformZoomOutStep * strength;
  } else if (cameraIntentRequestsZoomIn(normalized)) {
    defaults.zoom = NormalCameraTransformZoom + 0.5 * strength;
  } else if (containsText(normalized, "subject")) {
    defaults.zoom = 1.1;
  }

  return defaults;
}

std::optional<CameraTransformMotionKeyframes> NativeStewardPlanner::cameraMotionKeyframesForIntent(
  const std::string& intent,
  foundation::TimeRange activeRange
) const {
  if (activeRange.end.value <= activeRange.start.value) {
    return std::nullopt;
  }

  const std::string normalized = lowercaseAscii(intent);
  const double strength = intentStrengthMultiplier(normalized);
  if (!cameraIntentRequestsTemporalMotion(normalized)) {
    return std::nullopt;
  }

  if (containsAsciiWord(normalized, "left")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionXParam,
      CenteredCameraTransformPositionX,
      -CameraTransformPositionXStep * strength,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "right")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionXParam,
      CenteredCameraTransformPositionX,
      CameraTransformPositionXStep * strength,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "up")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionYParam,
      CenteredCameraTransformPositionY,
      -CameraTransformPositionYStep * strength,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "down")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionYParam,
      CenteredCameraTransformPositionY,
      CameraTransformPositionYStep * strength,
      activeRange.end
    };
  }

  if (cameraIntentRequestsZoomOut(normalized)) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::ZoomParam,
      NormalCameraTransformZoom,
      NormalCameraTransformZoom - CameraTransformZoomOutStep * strength,
      activeRange.end
    };
  }
  if (containsText(normalized, "zoom") || cameraIntentRequestsZoomIn(normalized)) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::ZoomParam,
      NormalCameraTransformZoom,
      NormalCameraTransformZoom + 0.5 * strength,
      activeRange.end
    };
  }

  return std::nullopt;
}

bool NativeStewardPlanner::cameraIntentRequestsExplicitMotion(const std::string& intent) const {
  return cameraIntentRequestsTemporalMotion(lowercaseAscii(intent));
}

bool NativeStewardPlanner::undoIntentTargetsLastEdit(const std::string& intent) const {
  return undoIntentRequestsLastEdit(lowercaseAscii(intent));
}

bool NativeStewardPlanner::redoIntentTargetsLastUndoneEdit(const std::string& intent) const {
  return redoIntentRequestsLastUndoneEdit(lowercaseAscii(intent));
}

bool NativeStewardPlanner::historyIntentTargetsEdit(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  return undoIntentRequestsLastEdit(normalized) ||
         redoIntentRequestsLastUndoneEdit(normalized);
}

bool NativeStewardPlanner::cameraTransformDeleteIntentTargetsCameraControls(const std::string& intent) const {
  return cameraTransformDeleteIntentRequestsControls(lowercaseAscii(intent));
}

bool NativeStewardPlanner::cameraUpdateIntentTargetsCamera(const std::string& intent) const {
  return cameraUpdateIntentRequestsCamera(lowercaseAscii(intent));
}

foundation::Result<CameraUpdateIntent> NativeStewardPlanner::cameraUpdateForIntent(
  const timeline::CameraPayload& current,
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  timeline::CameraPayload payload = current;
  bool changed = false;

  if (auto name = cameraNameEditFromIntent(intent, normalized)) {
    payload.name = name.value();
    changed = changed || payload.name != current.name;
  }

  if (cameraIntentRequestsFocalLength(normalized)) {
    const std::optional<double> focalLength = focalLengthFromIntent(intent, normalized);
    if (!focalLength.has_value() || focalLength.value() <= 0.0) {
      return foundation::Error{
        "steward.camera_focal_length_missing",
        "Camera focal length edits must include a positive numeric focal length."
      };
    }
    payload.state.lens.focalLength = focalLength.value();
    changed = changed || payload.state.lens.focalLength != current.state.lens.focalLength;
  }

  if (!changed) {
    return foundation::Error{
      "steward.camera_update_intent_unknown",
      "Camera update requests must explicitly rename the camera or set a focal length."
    };
  }

  return CameraUpdateIntent{payload, changed};
}

bool NativeStewardPlanner::clipEditIntentTargetsClip(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  if (clipDeleteIntentRequestsClip(normalized)) {
    return false;
  }
  if (!clipIntentMentionsClipTarget(normalized)) {
    return false;
  }
  return static_cast<bool>(clipEditForIntent(
    timeline::ClipPayload{
      timeline::ClipKind::Video,
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{2.0}},
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{2.0}},
      1.0,
      foundation::AssetId{"asset_steward_planner_default"},
      timeline::Transform2D{}
    },
    intent
  ));
}

bool NativeStewardPlanner::clipDeleteIntentTargetsClip(const std::string& intent) const {
  return clipDeleteIntentRequestsClip(lowercaseAscii(intent));
}

bool NativeStewardPlanner::clipTintIntentTargetsClip(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  return !clipDeleteIntentRequestsClip(normalized) &&
         clipTintIntentRequestsClip(normalized);
}

bool NativeStewardPlanner::clipExposureIntentTargetsClip(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  return !clipDeleteIntentRequestsClip(normalized) &&
         !clipTintIntentRequestsClip(normalized) &&
         clipExposureIntentRequestsClip(normalized);
}

ClipTintIntentDefaults NativeStewardPlanner::clipTintDefaultsForIntent(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  return ClipTintIntentDefaults{
    clipTintColorForIntent(normalized),
    clipTintAmountForIntent(normalized)
  };
}

foundation::Result<std::vector<ClipTintParamAdjustment>> NativeStewardPlanner::clipTintParamAdjustmentsForIntent(
  const timeline::EffectPayload& current,
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  std::vector<ClipTintParamAdjustment> adjustments;

  if (clipTintIntentRequestsColor(normalized)) {
    const timeline::ParamValue value{clipTintColorForIntent(normalized)};
    const std::optional<timeline::ParamValue> currentValue =
      effectParamValue(current, effects::builtin_effect::ClipTintColorParam);
    if (!currentValue.has_value()) {
      return foundation::Error{
        "steward.clip_tint_param_missing",
        "Clip Tint controls are missing the color parameter."
      };
    }
    if (currentValue.value() != value) {
      adjustments.push_back(ClipTintParamAdjustment{effects::builtin_effect::ClipTintColorParam, value});
    }
  }

  if (clipTintIntentRequestsAmount(normalized)) {
    const timeline::ParamValue value{clipTintAmountForIntent(normalized)};
    const std::optional<timeline::ParamValue> currentValue =
      effectParamValue(current, effects::builtin_effect::ClipTintAmountParam);
    if (!currentValue.has_value()) {
      return foundation::Error{
        "steward.clip_tint_param_missing",
        "Clip Tint controls are missing the amount parameter."
      };
    }
    if (currentValue.value() != value) {
      adjustments.push_back(ClipTintParamAdjustment{effects::builtin_effect::ClipTintAmountParam, value});
    }
  }

  if (adjustments.empty()) {
    return foundation::Error{
      "steward.clip_tint_noop",
      "Clip Tint controls already match the requested adjustment."
    };
  }

  return adjustments;
}

ClipExposureIntentDefaults NativeStewardPlanner::clipExposureDefaultsForIntent(const std::string& intent) const {
  return ClipExposureIntentDefaults{clipExposureForIntent(lowercaseAscii(intent))};
}

foundation::Result<std::vector<ClipExposureParamAdjustment>> NativeStewardPlanner::clipExposureParamAdjustmentsForIntent(
  const timeline::EffectPayload& current,
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  std::vector<ClipExposureParamAdjustment> adjustments;

  if (clipExposureIntentRequestsAmount(normalized)) {
    const timeline::ParamValue value{clipExposureForIntent(normalized)};
    const std::optional<timeline::ParamValue> currentValue =
      effectParamValue(current, effects::builtin_effect::ClipExposureParam);
    if (!currentValue.has_value()) {
      return foundation::Error{
        "steward.clip_exposure_param_missing",
        "Clip Exposure controls are missing the exposure parameter."
      };
    }
    if (currentValue.value() != value) {
      adjustments.push_back(ClipExposureParamAdjustment{effects::builtin_effect::ClipExposureParam, value});
    }
  }

  if (adjustments.empty()) {
    return foundation::Error{
      "steward.clip_exposure_noop",
      "Clip Exposure controls already match the requested adjustment."
    };
  }

  return adjustments;
}

bool NativeStewardPlanner::trackCreateIntentTargetsTrack(const std::string& intent) const {
  return trackCreateIntentRequestsTrack(lowercaseAscii(intent));
}

TrackIntentDefaults NativeStewardPlanner::trackDefaultsForIntent(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  const timeline::TrackKind kind = trackKindForIntent(normalized);
  return TrackIntentDefaults{
    kind == timeline::TrackKind::Audio ? "Audio Track" : "Video Track",
    kind
  };
}

bool NativeStewardPlanner::trackDeleteIntentTargetsTrack(const std::string& intent) const {
  return trackDeleteIntentRequestsTrack(lowercaseAscii(intent));
}

bool NativeStewardPlanner::textClipIntentTargetsText(const std::string& intent) const {
  return textIntentRequestsText(lowercaseAscii(intent));
}

TextClipIntentDefaults NativeStewardPlanner::textClipDefaultsForIntent(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  TextClipIntentDefaults defaults;
  defaults.text = quotedTextFromIntent(intent).value_or(unquotedTextFromIntent(intent, normalized));
  defaults.transform.position.y = textIntentRequestsLowerThird(normalized)
    ? LowerThirdTextPositionY
    : TitleTextPositionY;
  defaults.style.fontSize = textIntentRequestsLowerThird(normalized)
    ? LowerThirdTextFontSize
    : TitleTextFontSize;
  defaults.style.color = foundation::Vec3{1.0, 1.0, 1.0};
  return defaults;
}

bool NativeStewardPlanner::textClipEditIntentTargetsTextClip(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  return textIntentRequestsText(normalized) ||
         containsAsciiWord(normalized, "font") ||
         containsAsciiWord(normalized, "bigger") ||
         containsAsciiWord(normalized, "larger") ||
         containsAsciiWord(normalized, "smaller") ||
         containsAsciiWord(normalized, "move") ||
         containsAsciiWord(normalized, "left") ||
         containsAsciiWord(normalized, "right") ||
         containsAsciiWord(normalized, "up") ||
         containsAsciiWord(normalized, "down") ||
         containsAsciiWord(normalized, "longer") ||
         containsAsciiWord(normalized, "shorter") ||
         containsAsciiWord(normalized, "extend") ||
         containsAsciiWord(normalized, "shorten") ||
         textClipIntentRequestsOpacity(normalized);
}

foundation::Result<TextClipEditIntent> NativeStewardPlanner::textClipEditForIntent(
  const timeline::TextClipPayload& current,
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  timeline::TextClipPayload payload = current;
  bool changed = false;

  if (auto text = editedTextFromIntent(intent, normalized)) {
    payload.text = text.value();
    changed = payload.text != current.text;
  }

  if (textClipIntentRequestsFontSize(normalized)) {
    if (containsAsciiWord(normalized, "bigger") ||
        containsAsciiWord(normalized, "larger") ||
        containsText(normalized, "increase")) {
      payload.style.fontSize = current.style.fontSize * 1.25;
      changed = changed || payload.style.fontSize != current.style.fontSize;
    } else if (containsAsciiWord(normalized, "smaller") ||
               containsText(normalized, "decrease")) {
      payload.style.fontSize = current.style.fontSize * 0.75;
      changed = changed || payload.style.fontSize != current.style.fontSize;
    }
  }

  if (containsAsciiWord(normalized, "up")) {
    payload.transform.position.y = current.transform.position.y + CameraTransformPositionYStep;
    changed = changed || payload.transform.position.y != current.transform.position.y;
  } else if (containsAsciiWord(normalized, "down")) {
    payload.transform.position.y = current.transform.position.y - CameraTransformPositionYStep;
    changed = changed || payload.transform.position.y != current.transform.position.y;
  }

  if (containsAsciiWord(normalized, "left")) {
    payload.transform.position.x = current.transform.position.x - CameraTransformPositionXStep;
    changed = changed || payload.transform.position.x != current.transform.position.x;
  } else if (containsAsciiWord(normalized, "right")) {
    payload.transform.position.x = current.transform.position.x + CameraTransformPositionXStep;
    changed = changed || payload.transform.position.x != current.transform.position.x;
  }

  if (containsAsciiWord(normalized, "longer") ||
      containsAsciiWord(normalized, "extend")) {
    payload.timelineRange.end = foundation::TimeSeconds{current.timelineRange.end.value + ClipTimingStepSeconds};
    changed = changed || payload.timelineRange.end != current.timelineRange.end;
  } else if (containsAsciiWord(normalized, "shorter") ||
             containsAsciiWord(normalized, "shorten")) {
    payload.timelineRange.end = foundation::TimeSeconds{current.timelineRange.end.value - ClipTimingStepSeconds};
    if (payload.timelineRange.end.value <= payload.timelineRange.start.value) {
      return foundation::Error{
        "steward.text_clip_timing_invalid",
        "Text clip timing edits must leave a positive timeline range."
      };
    }
    changed = changed || payload.timelineRange.end != current.timelineRange.end;
  }

  if (containsAsciiWord(normalized, "hidden") ||
      containsAsciiWord(normalized, "hide")) {
    payload.transform.opacity = 0.0;
    changed = changed || payload.transform.opacity != current.transform.opacity;
  } else if (containsAsciiWord(normalized, "opaque") ||
             containsText(normalized, "full opacity") ||
             containsAsciiWord(normalized, "visible")) {
    payload.transform.opacity = 1.0;
    changed = changed || payload.transform.opacity != current.transform.opacity;
  } else if (containsAsciiWord(normalized, "transparent") ||
             containsAsciiWord(normalized, "fade") ||
             containsText(normalized, "half opacity")) {
    payload.transform.opacity = 0.5;
    changed = changed || payload.transform.opacity != current.transform.opacity;
  }

  if (!changed) {
    return foundation::Error{
      "steward.text_clip_edit_intent_unknown",
      "Text clip edit requests must explicitly change text, font size, position, timing, or opacity."
    };
  }

  return TextClipEditIntent{payload, changed};
}

bool NativeStewardPlanner::noteIntentTargetsNote(const std::string& intent) const {
  return noteIntentRequestsNote(lowercaseAscii(intent));
}

NoteIntentDefaults NativeStewardPlanner::noteDefaultsForIntent(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  NoteIntentDefaults defaults;
  defaults.title = quotedTextFromIntent(intent).value_or(std::string{DefaultNoteTitle});
  defaults.markdown = unquotedNoteTextFromIntent(intent, normalized);
  if (defaults.markdown.empty()) {
    defaults.markdown = defaults.title;
  }
  return defaults;
}

bool NativeStewardPlanner::noteEditIntentTargetsNote(const std::string& intent) const {
  const std::string normalized = lowercaseAscii(intent);
  return noteIntentRequestsNote(normalized) ||
         containsAsciiWord(normalized, "title") ||
         containsAsciiWord(normalized, "rename") ||
         containsAsciiWord(normalized, "body") ||
         containsAsciiWord(normalized, "markdown");
}

foundation::Result<NoteEditIntent> NativeStewardPlanner::noteEditForIntent(
  const timeline::NotePayload& current,
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  timeline::NotePayload payload = current;
  bool changed = false;

  if (auto title = noteTitleEditFromIntent(intent, normalized)) {
    payload.title = title.value();
    changed = changed || payload.title != current.title;
  }
  if (auto markdown = noteMarkdownEditFromIntent(intent, normalized)) {
    payload.markdown = markdown.value();
    changed = changed || payload.markdown != current.markdown;
  }

  if (!changed) {
    return foundation::Error{
      "steward.note_edit_intent_unknown",
      "Note edit requests must explicitly change the note title or body."
    };
  }

  return NoteEditIntent{payload, changed};
}

foundation::Result<ClipEditIntent> NativeStewardPlanner::clipEditForIntent(
  const timeline::ClipPayload& current,
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  const bool mixedTransformRequest = clipTransformOperationCount(normalized) > 1;
  const std::string movementClause = clipMovementClause(normalized);
  const double movementStrength = clipMovementStrengthMultiplier(normalized, movementClause, mixedTransformRequest);
  const double scaleStrength = clipScaleStrengthMultiplier(normalized, mixedTransformRequest);
  const double rotationStrength = clipRotationStrengthMultiplier(normalized, mixedTransformRequest);
  timeline::Transform2D transform = current.transform;
  bool transformChanged = false;
  double playbackRate = current.playbackRate;
  bool playbackRateChanged = false;
  std::optional<foundation::TimeSeconds> newStart;
  std::optional<foundation::TimeRange> timelineRange;
  std::optional<foundation::TimeRange> sourceRange;
  bool moveChanged = false;
  bool trimChanged = false;

  if (clipIntentRequestsMovement(normalized)) {
    if (containsAsciiWord(movementClause, "left")) {
      transform.position.x -= CameraTransformPositionXStep * movementStrength;
      transformChanged = true;
    } else if (containsAsciiWord(movementClause, "right")) {
      transform.position.x += CameraTransformPositionXStep * movementStrength;
      transformChanged = true;
    }

    if (containsAsciiWord(movementClause, "up")) {
      transform.position.y -= CameraTransformPositionYStep * movementStrength;
      transformChanged = true;
    } else if (containsAsciiWord(movementClause, "down")) {
      transform.position.y += CameraTransformPositionYStep * movementStrength;
      transformChanged = true;
    }
  }

  if (containsText(normalized, "scale down") ||
      containsAsciiWord(normalized, "smaller") ||
      containsAsciiWord(normalized, "shrink")) {
    transform.scale.x *= 1.0 - CameraTransformZoomInStep * scaleStrength;
    transform.scale.y *= 1.0 - CameraTransformZoomInStep * scaleStrength;
    transformChanged = true;
  } else if (containsText(normalized, "scale up") ||
             containsAsciiWord(normalized, "larger") ||
             containsAsciiWord(normalized, "bigger")) {
    transform.scale.x *= 1.0 + CameraTransformZoomInStep * scaleStrength;
    transform.scale.y *= 1.0 + CameraTransformZoomInStep * scaleStrength;
    transformChanged = true;
  }

  if (clipIntentRequestsRotation(normalized)) {
    if (containsAsciiWord(normalized, "straighten")) {
      transform.rotationDegrees = 0.0;
      transformChanged = true;
    } else if (containsAsciiWord(normalized, "left") ||
               containsAsciiWord(normalized, "counterclockwise") ||
               containsText(normalized, "counter clockwise")) {
      transform.rotationDegrees -= ClipRotationStepDegrees * rotationStrength;
      transformChanged = true;
    } else if (containsAsciiWord(normalized, "right") ||
               containsAsciiWord(normalized, "clockwise")) {
      transform.rotationDegrees += ClipRotationStepDegrees * rotationStrength;
      transformChanged = true;
    } else {
      transform.rotationDegrees += ClipRotationStepDegrees * rotationStrength;
      transformChanged = true;
    }
  }

  if (clipIntentRequestsHidden(normalized)) {
    transform.opacity = 0.0;
    transformChanged = true;
  } else if (clipIntentRequestsOpaque(normalized)) {
    transform.opacity = 1.0;
    transformChanged = true;
  } else if (containsAsciiWord(normalized, "fade") ||
      containsAsciiWord(normalized, "transparent") ||
      containsText(normalized, "half opacity")) {
    transform.opacity = 0.5;
    transformChanged = true;
  }

  if (containsText(normalized, "normal speed") ||
      containsText(normalized, "regular speed") ||
      (containsAsciiWord(normalized, "reset") && clipIntentRequestsPlaybackRate(normalized))) {
    playbackRate = 1.0;
    playbackRateChanged = playbackRate != current.playbackRate;
  } else if (containsText(normalized, "double speed")) {
    playbackRate = 2.0;
    playbackRateChanged = playbackRate != current.playbackRate;
  } else if (containsText(normalized, "half speed")) {
    playbackRate = 0.5;
    playbackRateChanged = playbackRate != current.playbackRate;
  } else if (containsText(normalized, "speed up") ||
             containsAsciiWord(normalized, "faster")) {
    playbackRate = current.playbackRate * 1.25;
    playbackRateChanged = true;
  } else if (containsText(normalized, "slow down") ||
             containsAsciiWord(normalized, "slower")) {
    playbackRate = current.playbackRate * 0.75;
    playbackRateChanged = true;
  }

  if (clipIntentRequestsMoveLater(normalized)) {
    newStart = foundation::TimeSeconds{current.timelineRange.start.value + ClipTimingStepSeconds};
    moveChanged = true;
  } else if (clipIntentRequestsMoveEarlier(normalized)) {
    newStart = foundation::TimeSeconds{current.timelineRange.start.value - ClipTimingStepSeconds};
    if (newStart->value < 0.0) {
      return foundation::Error{
        "steward.clip_timing_before_zero",
        "Clip timing edits cannot move a clip before the timeline start."
      };
    }
    moveChanged = true;
  }

  if (clipIntentRequestsTrimShorter(normalized) || clipIntentRequestsTrimLonger(normalized)) {
    const double timelineDelta = clipIntentRequestsTrimShorter(normalized)
      ? -ClipTimingStepSeconds
      : ClipTimingStepSeconds;
    timelineRange = foundation::TimeRange{
      current.timelineRange.start,
      foundation::TimeSeconds{current.timelineRange.end.value + timelineDelta}
    };
    sourceRange = foundation::TimeRange{
      current.sourceRange.start,
      foundation::TimeSeconds{current.sourceRange.end.value + timelineDelta * current.playbackRate}
    };
    if (timelineRange->end.value <= timelineRange->start.value ||
        sourceRange->end.value <= sourceRange->start.value) {
      return foundation::Error{
        "steward.clip_trim_range_invalid",
        "Clip timing edits must leave positive timeline and source ranges."
      };
    }
    trimChanged = true;
  }

  if (!moveChanged && !trimChanged && !transformChanged && !playbackRateChanged) {
    return foundation::Error{
      "steward.clip_edit_intent_unknown",
      "Clip edit requests must explicitly mention timing, movement, scale, rotation, opacity, or speed."
    };
  }

  return ClipEditIntent{
    transform,
    playbackRate,
    newStart,
    timelineRange,
    sourceRange,
    moveChanged,
    trimChanged,
    transformChanged,
    playbackRateChanged
  };
}

const timeline::EffectPayload* NativeStewardPlanner::cameraTransformEffectPayload(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  foundation::NodeId& effectNodeId
) const {
  for (const graph::GraphEdge& edge : snapshot.graph.edges()) {
    if (!edge.enabled ||
        edge.kind != graph::EdgeKind::Targets ||
        edge.targetNodeId != cameraNodeId) {
      continue;
    }
    const graph::GraphNode* effectNode = snapshot.graph.findNode(edge.sourceNodeId);
    if (effectNode == nullptr || effectNode->kind != graph::NodeKind::Effect) {
      continue;
    }
    const auto* payload = std::get_if<timeline::EffectPayload>(&effectNode->payload);
    if (payload == nullptr ||
        payload->displayName != effects::builtin_effect::CameraTransformDisplayName ||
        payload->implementation.kind != timeline::EffectImplementationKind::Builtin ||
        payload->implementation.entrypoint != effects::builtin_effect::CameraTransformEntrypoint) {
      continue;
    }

    effectNodeId = effectNode->id;
    return payload;
  }
  return nullptr;
}

foundation::Result<std::optional<foundation::KeyframeId>> NativeStewardPlanner::effectParamKeyframeIdAtTime(
  const timeline::EffectPayload& payload,
  const std::string& paramName,
  foundation::TimeSeconds time
) const {
  const auto param = std::find_if(payload.params.values.begin(), payload.params.values.end(), [&](const timeline::Param& value) {
    return value.name == paramName;
  });
  if (param == payload.params.values.end()) {
    return foundation::Error{
      "steward.camera_transform_param_missing",
      "Camera Transform controls are missing the requested parameter."
    };
  }

  for (const timeline::Param::Keyframe& keyframe : param->keyframes) {
    if (keyframe.time == time) {
      return std::optional<foundation::KeyframeId>{keyframe.id};
    }
  }
  return std::optional<foundation::KeyframeId>{};
}

foundation::Result<std::vector<CameraTransformParamAdjustment>> NativeStewardPlanner::cameraTransformParamAdjustmentsForIntent(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  const std::string& intent
) const {
  const graph::GraphNode* cameraNode = snapshot.graph.findNode(cameraNodeId);
  if (cameraNode == nullptr || cameraNode->kind != graph::NodeKind::Camera) {
    return foundation::Error{
      "steward.camera_missing",
      "Steward camera control adjustment requires an existing camera node."
    };
  }

  foundation::NodeId effectNodeId;
  const timeline::EffectPayload* payload = cameraTransformEffectPayload(snapshot, cameraNodeId, effectNodeId);
  if (payload == nullptr) {
    return foundation::Error{
      "steward.camera_transform_missing",
      "Steward camera control adjustment requires existing Camera Transform controls."
    };
  }

  const std::string normalized = lowercaseAscii(intent);
  const double strength = intentStrengthMultiplier(normalized);
  std::vector<CameraTransformParamAdjustment> adjustments;

  auto addAdjustment = [&](std::vector<CameraTransformParamAdjustment>& currentAdjustments,
                           std::string adjustmentParamName,
                           CameraTransformAdjustmentOperation adjustmentOperation,
                           double adjustmentOperand) -> foundation::Result<void> {
    auto adjustment = cameraTransformParamAdjustment(
      *payload,
      effectNodeId,
      std::move(adjustmentParamName),
      adjustmentOperation,
      adjustmentOperand
    );
    if (!adjustment) {
      return adjustment.error();
    }
    if (adjustment.value().has_value()) {
      currentAdjustments.push_back(std::move(adjustment.value().value()));
    }
    return {};
  };

  const bool centerRequested = cameraIntentRequestsCenter(normalized);
  const bool resetRequested = cameraIntentRequestsReset(normalized);
  if (centerRequested || resetRequested) {
    auto positionX = addAdjustment(
      adjustments,
      effects::builtin_effect::PositionXParam,
      CameraTransformAdjustmentOperation::Set,
      CenteredCameraTransformPositionX
    );
    if (!positionX) {
      return positionX.error();
    }
    auto positionY = addAdjustment(
      adjustments,
      effects::builtin_effect::PositionYParam,
      CameraTransformAdjustmentOperation::Set,
      CenteredCameraTransformPositionY
    );
    if (!positionY) {
      return positionY.error();
    }
    if (resetRequested) {
      auto zoom = addAdjustment(
        adjustments,
        effects::builtin_effect::ZoomParam,
        CameraTransformAdjustmentOperation::Set,
        NormalCameraTransformZoom
      );
      if (!zoom) {
        return zoom.error();
      }
      return adjustments;
    }
  }

  if (!centerRequested) {
    if (containsAsciiWord(normalized, "left")) {
      auto positionX = addAdjustment(
        adjustments,
        effects::builtin_effect::PositionXParam,
        CameraTransformAdjustmentOperation::Add,
        -CameraTransformPositionXStep * strength
      );
      if (!positionX) {
        return positionX.error();
      }
    } else if (containsAsciiWord(normalized, "right")) {
      auto positionX = addAdjustment(
        adjustments,
        effects::builtin_effect::PositionXParam,
        CameraTransformAdjustmentOperation::Add,
        CameraTransformPositionXStep * strength
      );
      if (!positionX) {
        return positionX.error();
      }
    }

    if (containsAsciiWord(normalized, "up")) {
      auto positionY = addAdjustment(
        adjustments,
        effects::builtin_effect::PositionYParam,
        CameraTransformAdjustmentOperation::Add,
        -CameraTransformPositionYStep * strength
      );
      if (!positionY) {
        return positionY.error();
      }
    } else if (containsAsciiWord(normalized, "down")) {
      auto positionY = addAdjustment(
        adjustments,
        effects::builtin_effect::PositionYParam,
        CameraTransformAdjustmentOperation::Add,
        CameraTransformPositionYStep * strength
      );
      if (!positionY) {
        return positionY.error();
      }
    }
  }

  if (cameraIntentRequestsZoomOut(normalized)) {
    auto zoom = addAdjustment(
      adjustments,
      effects::builtin_effect::ZoomParam,
      CameraTransformAdjustmentOperation::Multiply,
      1.0 - CameraTransformZoomOutStep * strength
    );
    if (!zoom) {
      return zoom.error();
    }
  } else if (cameraIntentRequestsZoomIn(normalized)) {
    auto zoom = addAdjustment(
      adjustments,
      effects::builtin_effect::ZoomParam,
      CameraTransformAdjustmentOperation::Multiply,
      1.0 + CameraTransformZoomInStep * strength
    );
    if (!zoom) {
      return zoom.error();
    }
  }

  if (adjustments.empty()) {
    return foundation::Error{
      "steward.camera_transform_intent_unknown",
      "Camera Transform adjustments must explicitly mention center, reset, left, right, up, down, zoom, bigger, or smaller."
    };
  }
  return adjustments;
}

foundation::Result<std::vector<CameraTransformKeyframeAdjustment>> NativeStewardPlanner::adjustedCameraTransformKeyframes(
  const project::ProjectSnapshot& snapshot,
  const CameraTransformParamAdjustment& adjustment
) const {
  const graph::GraphNode* effectNode = snapshot.graph.findNode(adjustment.effectNodeId);
  if (effectNode == nullptr || effectNode->kind != graph::NodeKind::Effect) {
    return foundation::Error{
      "steward.camera_transform_effect_missing",
      "Camera Transform keyframe adjustment requires an existing effect node."
    };
  }
  const auto* payload = std::get_if<timeline::EffectPayload>(&effectNode->payload);
  if (payload == nullptr) {
    return foundation::Error{
      "steward.camera_transform_effect_payload_missing",
      "Camera Transform keyframe adjustment requires an effect payload."
    };
  }

  const auto param = std::find_if(payload->params.values.begin(), payload->params.values.end(), [&](const timeline::Param& value) {
    return value.name == adjustment.paramName;
  });
  if (param == payload->params.values.end()) {
    return foundation::Error{
      "steward.camera_transform_param_missing",
      "Camera Transform controls are missing the requested parameter."
    };
  }

  std::vector<CameraTransformKeyframeAdjustment> keyframes;
  keyframes.reserve(param->keyframes.size());
  for (const timeline::Param::Keyframe& keyframe : param->keyframes) {
    const auto* numericValue = std::get_if<double>(&keyframe.value);
    if (numericValue == nullptr) {
      return foundation::Error{
        "steward.camera_transform_keyframe_not_numeric",
        "Camera Transform keyframe adjustment requires numeric keyframes."
      };
    }
    keyframes.push_back(CameraTransformKeyframeAdjustment{
      keyframe.id,
      keyframe.time,
      applyCameraTransformOperation(*numericValue, adjustment.operation, adjustment.operand)
    });
  }
  return keyframes;
}

foundation::Result<CameraTransformMotionKeyframes> NativeStewardPlanner::cameraTransformMotionAdjustmentForIntent(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  const std::string& intent,
  foundation::TimeRange activeRange
) const {
  if (activeRange.end.value <= activeRange.start.value) {
    return foundation::Error{
      "steward.camera_transform_motion_range_invalid",
      "Camera Transform motion adjustment requires a positive active range."
    };
  }

  auto adjustments = cameraTransformParamAdjustmentsForIntent(snapshot, cameraNodeId, intent);
  if (!adjustments) {
    return adjustments.error();
  }
  if (adjustments.value().empty()) {
    return foundation::Error{
      "steward.camera_transform_noop",
      "Camera Transform controls already match the requested adjustment."
    };
  }
  if (adjustments.value().size() != 1) {
    return foundation::Error{
      "steward.camera_transform_motion_multi_param",
      "Camera Transform motion adjustments must target one parameter."
    };
  }
  const CameraTransformParamAdjustment& adjustment = adjustments.value().front();

  foundation::NodeId effectNodeId;
  const timeline::EffectPayload* payload = cameraTransformEffectPayload(snapshot, cameraNodeId, effectNodeId);
  if (payload == nullptr) {
    return foundation::Error{
      "steward.camera_transform_missing",
      "Steward camera control adjustment requires existing Camera Transform controls."
    };
  }

  auto currentValue = numericEffectParamValue(*payload, adjustment.paramName);
  if (!currentValue) {
    return currentValue.error();
  }

  return CameraTransformMotionKeyframes{
    adjustment.paramName,
    currentValue.value(),
    adjustment.value,
    activeRange.end
  };
}

} // namespace grapple::app
