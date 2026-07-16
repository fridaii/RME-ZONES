//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_ITEM_ID_CONVERTER_H_
#define RME_MAP_ITEM_ID_CONVERTER_H_

#include "item_id_mapping.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class ItemIdConversionPolicy : uint8_t {
	Strict,
	Compatibility,
};

struct MapItemIdConversionOptions {
	std::filesystem::path source;
	std::filesystem::path destination;
	ItemIdMapping::Direction direction = ItemIdMapping::Direction::ServerToClient;
	ItemIdConversionPolicy policy = ItemIdConversionPolicy::Strict;
};

struct MapItemIdConversionIssue {
	uint16_t sourceId = 0;
	uint16_t preferredId = 0;
	uint64_t occurrences = 0;
	bool found = false;
	bool ambiguous = false;
	std::vector<uint16_t> candidates;
};

struct MapItemIdConversionReport {
	bool success = false;
	bool cancelled = false;
	bool outputValidated = false;
	uint64_t tileCount = 0;
	uint64_t totalItems = 0;
	uint64_t mappedItems = 0;
	uint64_t changedItems = 0;
	uint64_t missingItems = 0;
	uint64_t ambiguousItems = 0;
	std::string error;
	std::vector<std::string> warnings;
	std::vector<MapItemIdConversionIssue> issues;

	[[nodiscard]] std::string format(const MapItemIdConversionOptions& options) const;
};

[[nodiscard]] MapItemIdConversionReport ConvertMapItemIds(const MapItemIdConversionOptions& options);

#endif // RME_MAP_ITEM_ID_CONVERTER_H_
