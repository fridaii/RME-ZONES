//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_ITEM_ID_CONVERTER_WINDOW_H_
#define RME_MAP_ITEM_ID_CONVERTER_WINDOW_H_

#include "map_item_id_converter.h"

#include <wx/dialog.h>

class wxFilePickerCtrl;
class wxFileDirPickerEvent;
class wxRadioBox;
class wxStaticText;

class MapItemIdConverterWindow final : public wxDialog {
public:
	explicit MapItemIdConverterWindow(wxWindow* parent);

	[[nodiscard]] MapItemIdConversionOptions getOptions() const;

private:
	void updateDestination();
	void updatePolicyHelp();
	void onSourceChanged(wxFileDirPickerEvent& event);
	void onDirectionChanged(wxCommandEvent& event);
	void onPolicyChanged(wxCommandEvent& event);
	void onConfirm(wxCommandEvent& event);

	wxFilePickerCtrl* sourcePicker = nullptr;
	wxFilePickerCtrl* destinationPicker = nullptr;
	wxRadioBox* directionChoice = nullptr;
	wxRadioBox* policyChoice = nullptr;
	wxStaticText* policyHelp = nullptr;
	wxString generatedDestination;
};

[[nodiscard]] bool RunMapItemIdConverter(wxWindow* parent);

#endif // RME_MAP_ITEM_ID_CONVERTER_WINDOW_H_
