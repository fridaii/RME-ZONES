//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "map_item_id_converter_window.h"

#include "file_transaction.h"
#include "gui.h"
#include "item_id_mapping.h"

#include <wx/filepicker.h>
#include <wx/radiobox.h>

#include <system_error>

MapItemIdConverterWindow::MapItemIdConverterWindow(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Native OTBM Item ID Converter", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	auto* topSizer = newd wxBoxSizer(wxVERTICAL);

	auto* intro = newd wxStaticText(
		this,
		wxID_ANY,
		"Convert every ground, item and nested container ID using the embedded native C++ mapping. "
		"The source is never modified. Output is staged, reopened and structurally validated before commit."
	);
	intro->Wrap(680);
	topSizer->Add(intro, 0, wxEXPAND | wxALL, 14);

	auto* fileBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Files");
	auto* fileGrid = newd wxFlexGridSizer(2, 8, 12);
	fileGrid->AddGrowableCol(1, 1);
	fileGrid->Add(newd wxStaticText(fileBox->GetStaticBox(), wxID_ANY, "Source OTBM:"), 0, wxALIGN_CENTER_VERTICAL);
	sourcePicker = newd wxFilePickerCtrl(
		fileBox->GetStaticBox(), wxID_ANY, wxEmptyString, "Select the source OTBM map", "OpenTibia Binary Map (*.otbm)|*.otbm",
		wxDefaultPosition, wxDefaultSize, wxFLP_OPEN | wxFLP_FILE_MUST_EXIST | wxFLP_USE_TEXTCTRL
	);
	fileGrid->Add(sourcePicker, 1, wxEXPAND);
	fileGrid->Add(newd wxStaticText(fileBox->GetStaticBox(), wxID_ANY, "Destination:"), 0, wxALIGN_CENTER_VERTICAL);
	destinationPicker = newd wxFilePickerCtrl(
		fileBox->GetStaticBox(), wxID_ANY, wxEmptyString, "Select the output OTBM map", "OpenTibia Binary Map (*.otbm)|*.otbm",
		wxDefaultPosition, wxDefaultSize, wxFLP_SAVE | wxFLP_OVERWRITE_PROMPT | wxFLP_USE_TEXTCTRL
	);
	fileGrid->Add(destinationPicker, 1, wxEXPAND);
	fileBox->Add(fileGrid, 1, wxEXPAND | wxALL, 10);
	topSizer->Add(fileBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	wxString directions[] = { "Server ID -> Client ID", "Client ID -> Server ID" };
	directionChoice = newd wxRadioBox(this, wxID_ANY, "Direction", wxDefaultPosition, wxDefaultSize, 2, directions, 1, wxRA_SPECIFY_COLS);
	directionChoice->SetSelection(0);
	topSizer->Add(directionChoice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	wxString policies[] = { "Strict (lossless only)", "Compatibility (best effort)" };
	policyChoice = newd wxRadioBox(this, wxID_ANY, "Policy", wxDefaultPosition, wxDefaultSize, 2, policies, 1, wxRA_SPECIFY_COLS);
	policyChoice->SetSelection(0);
	topSizer->Add(policyChoice, 0, wxEXPAND | wxLEFT | wxRIGHT, 14);
	policyHelp = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	policyHelp->Wrap(680);
	topSizer->Add(policyHelp, 0, wxEXPAND | wxALL, 14);

	const auto statistics = ItemIdMapping::statistics();
	wxString mappingSummary;
	mappingSummary << "Embedded mapping: " << statistics.mapping_count << " forward pairs, "
				   << statistics.unique_client_count << " unique client IDs, "
				   << statistics.collision_count << " ambiguous client IDs. No external runtime or scripts are used.";
	auto* mappingLabel = newd wxStaticText(this, wxID_ANY, mappingSummary);
	mappingLabel->Wrap(680);
	topSizer->Add(mappingLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	wxSizer* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
	if (buttons) {
		if (wxWindow* confirm = FindWindow(wxID_OK)) {
			confirm->SetLabel("Convert and validate");
		}
		topSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);
	}

	SetSizerAndFit(topSizer);
	SetMinSize(wxSize(740, GetSize().GetHeight()));
	CentreOnParent();

	sourcePicker->Bind(wxEVT_FILEPICKER_CHANGED, &MapItemIdConverterWindow::onSourceChanged, this);
	directionChoice->Bind(wxEVT_RADIOBOX, &MapItemIdConverterWindow::onDirectionChanged, this);
	policyChoice->Bind(wxEVT_RADIOBOX, &MapItemIdConverterWindow::onPolicyChanged, this);
	Bind(wxEVT_BUTTON, &MapItemIdConverterWindow::onConfirm, this, wxID_OK);
	updatePolicyHelp();
}

MapItemIdConversionOptions MapItemIdConverterWindow::getOptions() const {
	MapItemIdConversionOptions options;
	options.source = std::filesystem::u8path(nstr(sourcePicker->GetPath()));
	options.destination = std::filesystem::u8path(nstr(destinationPicker->GetPath()));
	options.direction = directionChoice->GetSelection() == 0 ? ItemIdMapping::Direction::ServerToClient : ItemIdMapping::Direction::ClientToServer;
	options.policy = policyChoice->GetSelection() == 0 ? ItemIdConversionPolicy::Strict : ItemIdConversionPolicy::Compatibility;
	return options;
}

void MapItemIdConverterWindow::updateDestination() {
	const wxString source = sourcePicker->GetPath();
	if (source.empty()) {
		return;
	}
	const wxString currentDestination = destinationPicker->GetPath();
	if (!currentDestination.empty() && currentDestination != generatedDestination) {
		return;
	}

	wxFileName destination(source);
	destination.SetName(destination.GetName() + (directionChoice->GetSelection() == 0 ? "-client-ids" : "-server-ids"));
	destination.SetExt("otbm");
	generatedDestination = destination.GetFullPath();
	destinationPicker->SetPath(generatedDestination);
}

void MapItemIdConverterWindow::updatePolicyHelp() {
	if (policyChoice->GetSelection() == 0) {
		policyHelp->SetLabel("Strict mode stops before writing when any ID is missing or maps ambiguously. Use it when a lossless round trip is required.");
	} else {
		policyHelp->SetLabel("Compatibility mode keeps unmapped IDs unchanged and uses the legacy preferred server ID for reverse collisions. Every exception is listed in the final report.");
	}
	Layout();
}

void MapItemIdConverterWindow::onSourceChanged(wxFileDirPickerEvent& WXUNUSED(event)) {
	updateDestination();
}

void MapItemIdConverterWindow::onDirectionChanged(wxCommandEvent& WXUNUSED(event)) {
	updateDestination();
}

void MapItemIdConverterWindow::onPolicyChanged(wxCommandEvent& WXUNUSED(event)) {
	updatePolicyHelp();
}

void MapItemIdConverterWindow::onConfirm(wxCommandEvent& WXUNUSED(event)) {
	const MapItemIdConversionOptions options = getOptions();
	std::error_code filesystemError;
	if (options.source.empty() || !std::filesystem::is_regular_file(options.source, filesystemError) || filesystemError) {
		wxMessageBox("Select an existing source .otbm file.", "Invalid source", wxOK | wxICON_ERROR, this);
		return;
	}
	if (options.destination.empty()) {
		wxMessageBox("Select a destination .otbm file.", "Missing destination", wxOK | wxICON_ERROR, this);
		return;
	}
	if (as_lower_str(options.source.extension().string()) != ".otbm" || as_lower_str(options.destination.extension().string()) != ".otbm") {
		wxMessageBox("Source and destination must use the .otbm extension.", "Invalid file type", wxOK | wxICON_ERROR, this);
		return;
	}
	if (FileSaveTransaction::PathsReferToSameFile(options.source, options.destination)) {
		wxMessageBox("Source and destination must be different files.", "Unsafe destination", wxOK | wxICON_ERROR, this);
		return;
	}
	filesystemError.clear();
	const bool destinationExists = std::filesystem::exists(options.destination, filesystemError);
	if (filesystemError) {
		wxMessageBox("The destination cannot be inspected.", "Invalid destination", wxOK | wxICON_ERROR, this);
		return;
	}
	if (destinationExists && wxMessageBox("The destination already exists. Replace it after successful validation?", "Confirm replacement", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
		return;
	}
	EndModal(wxID_OK);
}

bool RunMapItemIdConverter(wxWindow* parent) {
	MapItemIdConverterWindow dialog(parent);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const MapItemIdConversionOptions options = dialog.getOptions();
	const MapItemIdConversionReport report = ConvertMapItemIds(options);
	const wxString title = report.success ? "OTBM Item ID Conversion Complete" : (report.cancelled ? "OTBM Item ID Conversion Cancelled" : "OTBM Item ID Conversion Failed");
	g_gui.ShowTextBox(parent, title, wxstr(report.format(options)));
	return report.success;
}
