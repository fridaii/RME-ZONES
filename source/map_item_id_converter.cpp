//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "map_item_id_converter.h"

#include "complexitem.h"
#include "file_transaction.h"
#include "gui.h"
#include "iomap_otbm.h"
#include "map.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>

namespace {
	constexpr std::size_t ItemIdDomainSize = 1u << 16;

	wxString PathToWxString(const std::filesystem::path& path) {
#ifdef _WIN32
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	std::string PrintablePath(const std::filesystem::path& path) {
		return nstr(PathToWxString(path));
	}

	class ReportBuilder {
	public:
		explicit ReportBuilder(MapItemIdConversionReport& report) :
			report(report),
			occurrences(ItemIdDomainSize, 0) { }

		void observe(const ItemIdMapping::Result& result) {
			++report.totalItems;
			++occurrences[result.original];
			if (result.found) {
				++report.mappedItems;
				if (result.converted != result.original) {
					++report.changedItems;
				}
			} else {
				++report.missingItems;
			}
			if (result.ambiguous) {
				++report.ambiguousItems;
			}
		}

		void finish(ItemIdMapping::Direction direction) {
			for (std::size_t id = 0; id < occurrences.size(); ++id) {
				if (occurrences[id] == 0) {
					continue;
				}
				const auto result = ItemIdMapping::convert(static_cast<uint16_t>(id), direction);
				if (result.found && !result.ambiguous) {
					continue;
				}

				MapItemIdConversionIssue issue;
				issue.sourceId = result.original;
				issue.preferredId = result.converted;
				issue.occurrences = occurrences[id];
				issue.found = result.found;
				issue.ambiguous = result.ambiguous;
				issue.candidates.assign(result.candidates.begin(), result.candidates.end());
				report.issues.push_back(std::move(issue));
			}
		}

	private:
		MapItemIdConversionReport& report;
		std::vector<uint64_t> occurrences;
	};

	class MappingCodec final : public ItemIdCodec {
	public:
		MappingCodec(ItemIdMapping::Direction direction, ReportBuilder* reportBuilder = nullptr) :
			direction(direction),
			reportBuilder(reportBuilder) { }

		bool Decode(uint16_t storedId, uint16_t& serverId) const override {
			const auto result = ItemIdMapping::convert(storedId, direction);
			if (reportBuilder) {
				reportBuilder->observe(result);
			}
			serverId = result.converted;
			return true;
		}

		bool Encode(uint16_t serverId, uint16_t& storedId) const override {
			const auto result = ItemIdMapping::convert(serverId, direction);
			if (reportBuilder) {
				reportBuilder->observe(result);
			}
			storedId = result.converted;
			return true;
		}

	private:
		ItemIdMapping::Direction direction;
		ReportBuilder* reportBuilder;
	};

	class StableHash {
	public:
		template <typename Value>
		void add(const Value& value) {
			const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
			for (std::size_t index = 0; index < sizeof(Value); ++index) {
				hash ^= bytes[index];
				hash *= 1099511628211ull;
			}
		}

		[[nodiscard]] uint64_t value() const {
			return hash;
		}

	private:
		uint64_t hash = 14695981039346656037ull;
	};

	struct MapSummary {
		uint16_t width = 0;
		uint16_t height = 0;
		uint64_t tileCount = 0;
		uint64_t itemCount = 0;
		uint64_t containerCount = 0;
		uint64_t hash = 0;

		[[nodiscard]] bool operator==(const MapSummary& other) const = default;
	};

	struct PendingItem {
		Item* item;
		uint32_t depth;
		uint32_t index;
		uint8_t role;
	};

	uint16_t StoredIdForSummary(uint16_t serverId, bool encodeServerToClient) {
		return encodeServerToClient ? ItemIdMapping::serverToClient(serverId).converted : serverId;
	}

	bool AnalyzeMap(Map& map, bool encodeServerToClient, ReportBuilder* reportBuilder, MapSummary& summary) {
		summary.width = static_cast<uint16_t>(map.getWidth());
		summary.height = static_cast<uint16_t>(map.getHeight());
		StableHash hash;
		hash.add(summary.width);
		hash.add(summary.height);

		std::vector<PendingItem> pending;
		pending.reserve(64);
		uint64_t processedItems = 0;
		const uint64_t expectedTiles = std::max<uint64_t>(1, map.getTileCount());

		auto analyzeItem = [&](Item* root, uint8_t role, uint32_t rootIndex) {
			pending.clear();
			pending.push_back({ root, 0, rootIndex, role });
			while (!pending.empty()) {
				const PendingItem current = pending.back();
				pending.pop_back();
				if (!current.item) {
					continue;
				}

				const auto mapping = ItemIdMapping::serverToClient(current.item->getID());
				if (reportBuilder) {
					reportBuilder->observe(mapping);
				}
				const uint16_t storedId = StoredIdForSummary(current.item->getID(), encodeServerToClient);
				hash.add(current.role);
				hash.add(current.depth);
				hash.add(current.index);
				hash.add(storedId);
				hash.add(current.item->getSubtype());
				hash.add(current.item->getActionID());
				hash.add(current.item->getUniqueID());
				hash.add(current.item->getTier());
				++summary.itemCount;
				++processedItems;
				if (processedItems % 8192 == 0 && !g_gui.SetLoadDone(static_cast<int32_t>(std::min<uint64_t>(99, summary.tileCount * 100 / expectedTiles)))) {
					return false;
				}

				auto* container = dynamic_cast<Container*>(current.item);
				const uint32_t childCount = container ? static_cast<uint32_t>(container->getItemCount()) : 0;
				hash.add(childCount);
				if (container) {
					++summary.containerCount;
					for (uint32_t childIndex = childCount; childIndex > 0; --childIndex) {
						pending.push_back({ container->getItem(childIndex - 1), current.depth + 1, childIndex - 1, 2 });
					}
				}
			}
			return true;
		};

		for (MapIterator iterator = map.begin(); iterator != map.end(); ++iterator) {
			Tile* tile = (*iterator)->get();
			if (!tile) {
				continue;
			}
			++summary.tileCount;
			const Position& position = tile->getPosition();
			hash.add(position.x);
			hash.add(position.y);
			hash.add(position.z);
			hash.add(tile->getMapFlags());
			hash.add(tile->getHouseID());

			const uint8_t hasGround = tile->ground ? 1 : 0;
			hash.add(hasGround);
			if (tile->ground) {
				if (!analyzeItem(tile->ground, 0, 0)) {
					return false;
				}
			}
			for (uint32_t index = 0; index < tile->items.size(); ++index) {
				if (!analyzeItem(tile->items[index], 1, index)) {
					return false;
				}
			}

			if ((summary.tileCount % 4096 == 0 || processedItems % 8192 == 0) && !g_gui.SetLoadDone(static_cast<int32_t>(std::min<uint64_t>(99, summary.tileCount * 100 / expectedTiles)))) {
				return false;
			}
		}

		summary.hash = hash.value();
		return true;
	}

	bool FilesMatch(const std::filesystem::path& leftPath, const std::filesystem::path& rightPath, std::string& error) {
		error.clear();
		std::error_code filesystemError;
		const uintmax_t leftSize = std::filesystem::file_size(leftPath, filesystemError);
		if (filesystemError) {
			error = "Could not inspect first validation file: " + filesystemError.message();
			return false;
		}
		const uintmax_t rightSize = std::filesystem::file_size(rightPath, filesystemError);
		if (filesystemError) {
			error = "Could not inspect second validation file: " + filesystemError.message();
			return false;
		}
		if (leftSize != rightSize) {
			error = "Round-trip output size changed.";
			return false;
		}

		std::ifstream left(leftPath, std::ios::binary);
		std::ifstream right(rightPath, std::ios::binary);
		if (!left || !right) {
			error = "Could not reopen round-trip output for byte comparison.";
			return false;
		}

		std::array<char, 64 * 1024> leftBuffer;
		std::array<char, 64 * 1024> rightBuffer;
		while (left && right) {
			left.read(leftBuffer.data(), leftBuffer.size());
			right.read(rightBuffer.data(), rightBuffer.size());
			const std::streamsize leftRead = left.gcount();
			const std::streamsize rightRead = right.gcount();
			if (leftRead != rightRead || !std::equal(leftBuffer.begin(), leftBuffer.begin() + leftRead, rightBuffer.begin())) {
				error = "Round-trip output bytes changed.";
				return false;
			}
		}
		if (left.bad() || right.bad()) {
			error = "Could not complete round-trip byte comparison.";
			return false;
		}
		return true;
	}

	void AppendWarnings(IOMapOTBM& io, MapItemIdConversionReport& report) {
		for (const wxString& warning : io.getWarnings()) {
			report.warnings.push_back(nstr(warning));
		}
	}

	bool IsCancellationError(const wxString& error) {
		return error.Lower().Contains("cancel");
	}
}

std::string MapItemIdConversionReport::format(const MapItemIdConversionOptions& options) const {
	std::ostringstream output;
	output << "Native OTBM Item ID Conversion\n"
		   << "Status: " << (success ? "SUCCESS" : (cancelled ? "CANCELLED" : "FAILED")) << '\n'
		   << "Direction: " << (options.direction == ItemIdMapping::Direction::ServerToClient ? "Server ID -> Client ID" : "Client ID -> Server ID") << '\n'
		   << "Policy: " << (options.policy == ItemIdConversionPolicy::Strict ? "Strict" : "Compatibility") << '\n'
		   << "Source: " << PrintablePath(options.source) << '\n'
		   << "Destination: " << PrintablePath(options.destination) << '\n'
		   << "Mapping: " << ItemIdMapping::sourceVersion() << '\n'
		   << "Tiles: " << tileCount << '\n'
		   << "Items scanned: " << totalItems << '\n'
		   << "Mapped: " << mappedItems << '\n'
		   << "Changed: " << changedItems << '\n'
		   << "Missing: " << missingItems << '\n'
		   << "Ambiguous: " << ambiguousItems << '\n'
		   << "Output reopened and validated: " << (outputValidated ? "yes" : "no") << '\n';
	if (success && (missingItems != 0 || ambiguousItems != 0)) {
		output << "Result note: decisions below are expected mapping collisions, not conversion failures.\n";
	}

	if (!error.empty()) {
		output << "\nError: " << error << '\n';
	}
	if (!issues.empty()) {
		output << (success ? "\nMapping decisions (source ID, occurrences, resolution):\n" : "\nBlocking issues (source ID, occurrences, resolution):\n");
		for (const auto& issue : issues) {
			output << "  " << issue.sourceId << " x" << issue.occurrences << ": ";
			if (!issue.found) {
				output << "not mapped; kept unchanged";
			} else {
				output << "preferred " << issue.preferredId;
			}
			if (issue.ambiguous) {
				output << "; server candidates [";
				for (std::size_t index = 0; index < issue.candidates.size(); ++index) {
					if (index != 0) {
						output << ", ";
					}
					output << issue.candidates[index];
				}
				output << ']';
			}
			output << '\n';
		}
	}
	if (!warnings.empty()) {
		output << "\nWarnings:\n";
		for (const std::string& warning : warnings) {
			output << "  - " << warning << '\n';
		}
	}
	return output.str();
}

MapItemIdConversionReport ConvertMapItemIds(const MapItemIdConversionOptions& options) {
	MapItemIdConversionReport report;
	if (!ItemIdMapping::validateTables()) {
		report.error = "Embedded item ID mapping tables failed validation.";
		return report;
	}
	if (options.source.empty() || options.destination.empty()) {
		report.error = "Source and destination paths are required.";
		return report;
	}
	if (FileSaveTransaction::PathsReferToSameFile(options.source, options.destination)) {
		report.error = "Source and destination must be different files.";
		return report;
	}
	std::error_code filesystemError;
	if (!std::filesystem::is_regular_file(options.source, filesystemError) || filesystemError) {
		report.error = "Source OTBM file does not exist or cannot be accessed.";
		return report;
	}
	const std::filesystem::path destinationDirectory = options.destination.parent_path();
	if (!destinationDirectory.empty()) {
		std::filesystem::create_directories(destinationDirectory, filesystemError);
		if (filesystemError) {
			report.error = "Could not create destination directory: " + filesystemError.message();
			return report;
		}
	}

	MapVersion sourceVersion;
	if (!IOMapOTBM::getVersionInfo(FileName(PathToWxString(options.source)), sourceVersion)) {
		report.error = "Source is not a valid supported OTBM file.";
		return report;
	}

	ScopedLoadingBar loading("Converting OTBM item IDs...", true);
	ReportBuilder reportBuilder(report);
	std::unique_ptr<Map> map = std::make_unique<Map>();
	MappingCodec reverseReadCodec(ItemIdMapping::Direction::ClientToServer, &reportBuilder);
	IOMapOTBM loader(sourceVersion);
	if (options.direction == ItemIdMapping::Direction::ClientToServer) {
		loader.useItemIdCodec(&reverseReadCodec);
	}

	ScopedLoadingBar::SetLoadScale(0, 30);
	if (!loader.loadMapData(*map, FileName(PathToWxString(options.source)))) {
		AppendWarnings(loader, report);
		report.cancelled = IsCancellationError(loader.getError());
		report.error = report.cancelled ? "Conversion cancelled while loading the source map." : nstr(loader.getError());
		return report;
	}
	AppendWarnings(loader, report);

	ScopedLoadingBar::SetLoadScale(30, 50);
	MapSummary sourceSummary;
	const bool encodeOutputIds = options.direction == ItemIdMapping::Direction::ServerToClient;
	ReportBuilder* analysisReport = encodeOutputIds ? &reportBuilder : nullptr;
	if (!AnalyzeMap(*map, encodeOutputIds, analysisReport, sourceSummary)) {
		report.cancelled = true;
		report.error = "Conversion cancelled during map analysis.";
		return report;
	}
	report.tileCount = sourceSummary.tileCount;
	reportBuilder.finish(options.direction);

	if (options.direction == ItemIdMapping::Direction::ClientToServer && report.missingItems != 0) {
		report.error = "Reverse conversion stopped safely: unmapped Client IDs have no reliable Server ID type information, so preserving their nested data cannot be guaranteed.";
		return report;
	}
	const uint64_t blockingAmbiguities = options.direction == ItemIdMapping::Direction::ClientToServer ? report.ambiguousItems : 0;
	if (options.policy == ItemIdConversionPolicy::Strict && (report.missingItems != 0 || blockingAmbiguities != 0)) {
		std::ostringstream error;
		error << "Strict policy stopped conversion: " << report.missingItems << " unmapped and " << blockingAmbiguities << " ambiguous item occurrence(s).";
		report.error = error.str();
		return report;
	}

	FileSaveTransaction transaction;
	const std::filesystem::path stagedPath = transaction.Stage(options.destination);
	MappingCodec forwardWriteCodec(ItemIdMapping::Direction::ServerToClient);
	IOMapOTBM saver(map->getVersion());
	if (encodeOutputIds) {
		saver.useItemIdCodec(&forwardWriteCodec);
	}

	ScopedLoadingBar::SetLoadScale(50, 75);
	if (!saver.saveMapData(*map, FileName(PathToWxString(stagedPath)))) {
		AppendWarnings(saver, report);
		report.cancelled = IsCancellationError(saver.getError());
		report.error = report.cancelled ? "Conversion cancelled while writing the output map." : nstr(saver.getError());
		return report;
	}
	AppendWarnings(saver, report);

	map.reset();
	if (!ScopedLoadingBar::SetLoadDone(99, "Reopening generated OTBM...")) {
		report.cancelled = true;
		report.error = "Conversion cancelled before output validation.";
		return report;
	}

	ScopedLoadingBar::SetLoadScale(75, 90);
	std::unique_ptr<Map> validationMap = std::make_unique<Map>();
	MappingCodec validationReadCodec(ItemIdMapping::Direction::ClientToServer);
	IOMapOTBM validator(sourceVersion);
	if (encodeOutputIds) {
		validator.useItemIdCodec(&validationReadCodec);
	}
	if (!validator.loadMapData(*validationMap, FileName(PathToWxString(stagedPath)))) {
		AppendWarnings(validator, report);
		report.cancelled = IsCancellationError(validator.getError());
		report.error = report.cancelled ? "Conversion cancelled while validating the output map." : "Generated OTBM could not be reopened: " + nstr(validator.getError());
		return report;
	}
	AppendWarnings(validator, report);

	ScopedLoadingBar::SetLoadScale(90, 94);
	MapSummary validationSummary;
	if (!AnalyzeMap(*validationMap, encodeOutputIds, nullptr, validationSummary)) {
		report.cancelled = true;
		report.error = "Conversion cancelled during output validation.";
		return report;
	}
	if (!(sourceSummary == validationSummary)) {
		report.error = "Generated OTBM failed structural validation; no destination file was replaced.";
		return report;
	}

	ScopedLoadingBar::SetLoadScale(94, 98);
	FileSaveTransaction roundTripTransaction;
	const std::filesystem::path roundTripPath = roundTripTransaction.Stage(options.destination);
	MappingCodec validationWriteCodec(ItemIdMapping::Direction::ServerToClient);
	IOMapOTBM validationSaver(validationMap->getVersion());
	if (encodeOutputIds) {
		validationSaver.useItemIdCodec(&validationWriteCodec);
	}
	if (!validationSaver.saveMapData(*validationMap, FileName(PathToWxString(roundTripPath)))) {
		AppendWarnings(validationSaver, report);
		report.cancelled = IsCancellationError(validationSaver.getError());
		report.error = report.cancelled ? "Conversion cancelled during round-trip validation." : "Generated OTBM failed native round-trip save: " + nstr(validationSaver.getError());
		return report;
	}
	AppendWarnings(validationSaver, report);
	std::string comparisonError;
	if (!FilesMatch(stagedPath, roundTripPath, comparisonError)) {
		report.error = "Generated OTBM failed byte-exact round-trip validation: " + comparisonError;
		return report;
	}
	report.outputValidated = true;
	validationMap.reset();

	std::string commitError;
	if (!transaction.Commit(commitError)) {
		report.error = commitError;
		return report;
	}

	report.success = true;
	ScopedLoadingBar::SetLoadDone(99, "Conversion complete.");
	return report;
}
