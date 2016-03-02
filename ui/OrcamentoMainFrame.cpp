#include <string>
#include <vector>
#include <wx/msgdlg.h>
#include <wx/aboutdlg.h>
#include "CSV.hpp"
#include "OrcamentoMainFrame.h"
#include "CreateDatabaseDialog.h"
#include "ExecutionDialog.h"
#include "WalletOverviewDialog.h"

#include "model/actions/CopySelectedEstimateTo.h"
#include "model/actions/DeleteEstimate.h"
#include "model/actions/InsertBudget.h"
#include "model/actions/InsertEstimate.h"
#include "model/actions/ExecuteNextBudget.h"
#include "model/actions/UpdateEstimate.h"

// helper functions
enum wxbuildinfoformat { short_f, long_f };

wxString wxbuildinfo(wxbuildinfoformat format)
{
    if(format == long_f) {
        wxString wxbuild;
#if defined(__WXMSW__)
        wxbuild << _T("-Windows");
#elif defined(__UNIX__)
        wxbuild << _T("-Linux");
#endif

#if wxUSE_UNICODE
        wxbuild << _T("-Unicode build");
#else
        wxbuild << _T("-ANSI build");
#endif // wxUSE_UNICODE
        return wxbuild;
    }
    return wxVERSION_STRING;
}

inline wxString isoDate()
{
    wxString date(__DATE__), time(__TIME__);
    wxDateTime isoDate;
    isoDate.ParseFormat(date + " " + time, "%b %d %Y %T");
    return isoDate.FormatISOCombined();
}

namespace EstimateColumn
{
constexpr int ID = 0, NAME = 1, DUE = 2, ESTIMATED = 3, ACCOUNTED = 4, REMAINING = 5, CATEGORY = 6, OBS = 7;
constexpr int length = 8;
};

OrcamentoMainFrame::OrcamentoMainFrame(wxWindow* parent)
    : OrcamentoMainFrameBase(parent), _sort(EstimateColumn::CATEGORY)
{
    gdEstimates->AppendCols(8);
    gdEstimates->HideCol(0);
    gdEstimates->EnableEditing(true);
    gdEstimates->EnableGridLines(true);
    gdEstimates->SetColLabelValue(EstimateColumn::ID, _("Id"));
    gdEstimates->SetColLabelValue(EstimateColumn::NAME, _("Name"));
    gdEstimates->SetColLabelValue(EstimateColumn::DUE, _("Due Day"));
    gdEstimates->SetColLabelValue(EstimateColumn::ESTIMATED, _("Estimated"));
    gdEstimates->SetColLabelValue(EstimateColumn::ACCOUNTED, _("Accounted"));
    gdEstimates->SetColLabelValue(EstimateColumn::REMAINING, _("Remaining"));
    gdEstimates->SetColLabelValue(EstimateColumn::CATEGORY, _("Category"));
    gdEstimates->SetColLabelValue(EstimateColumn::OBS, _("Observation"));
    
    gdEstimates->SetSortingColumn(EstimateColumn::CATEGORY);

    SetupCellAttr();
    wxArrayString ss;
    ss.Add(_("Load a existing Database"));
    ss.Add(_("Create a new database"));
CHOOSE_OPTION://An ugly fix for issue #22. Should replace it ASAP for a proper start window...
    auto a = wxGetSingleChoiceIndex(_("Choose what you want to do"), _("You want to..."), ss, this);
    switch(a){
        case -1:
            Close();
            break;
        case 0:{
            wxFileDialog openFileDialog(
                this, _("Select the file"), "", "", "Orca files (*.orca)|*.orca", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if(openFileDialog.ShowModal() != wxID_OK) {
                return;
            }
            wxString location = openFileDialog.GetPath();
            try {
                _document = OrcaDocument::load(location);
                RefreshModel();
            } catch(const wrongver_error& e) {
                if(OrcaDocument::canConvert(e.major, e.minor, e.patch, e.variant)) {
                    try {
                        wxString backupLocation = location + ".old";
                        if(rename(location, backupLocation)!=0){
                            std::runtime_error("Can not backup.");
                        }
                        _document = OrcaDocument::convertFrom0_1_0(backupLocation, location);
                        RefreshModel();
                    } catch(const std::exception& e) {
                        wxMessageBox(e.what());
                        goto CHOOSE_OPTION;
                    }
                } else {
                    wxMessageBox(e.what());
                    goto CHOOSE_OPTION;
                }
            } catch(const std::exception& e) {
                wxMessageBox(e.what());
                goto CHOOSE_OPTION;
            }
            break;
        }
        case 1:{
            CreateDatabaseDialog dialog(this);
DIALOG_SHOW: //Don't do this at home, kids.
            if(dialog.ShowModal() != wxID_OK ) {
                goto CHOOSE_OPTION;
            }
            wxString location = dialog.getLocation();
            if(location.Trim().length()==0) {
                wxMessageBox("Invalid Path");
                goto DIALOG_SHOW;
            }
            wxDateTime start  = dialog.getStart();

            try {
                std::remove(location);
                _document = OrcaDocument::create(location, start);
            } catch (const std::exception &e) {
                wxMessageBox(e.what());
                _document = nullptr;
                goto DIALOG_SHOW;
            }
            RefreshModel();
            break;
        }
    }
}

OrcamentoMainFrame::~OrcamentoMainFrame()
{
}

void OrcamentoMainFrame::SetupCellAttr()
{
    // Columns
    auto moneyRenderer = new wxGridCellFloatRenderer(-1, 2);
    // Due
    wxGridCellAttr* attrDueCol = new wxGridCellAttr();
    attrDueCol->SetRenderer(new wxGridCellNumberRenderer());
    attrDueCol->SetEditor(new wxGridCellNumberEditor(1, 31));
    gdEstimates->SetColAttr(EstimateColumn::DUE, attrDueCol);

    // Expected
    wxGridCellAttr* attrExpectedCol = new wxGridCellAttr();
    attrExpectedCol->SetRenderer(moneyRenderer);
    attrExpectedCol->SetEditor(new wxGridCellFloatEditor(-1, 2));
    gdEstimates->SetColAttr(EstimateColumn::ESTIMATED, attrExpectedCol);

    // Accounted
    moneyRenderer->IncRef();
    wxGridCellAttr* attrAccountedCol = new wxGridCellAttr();
    attrAccountedCol->SetReadOnly(true);
    attrAccountedCol->SetRenderer(moneyRenderer);
    gdEstimates->SetColAttr(EstimateColumn::ACCOUNTED, attrAccountedCol);

    // Remaining
    moneyRenderer->IncRef();
    wxGridCellAttr* attrRemainingCol = new wxGridCellAttr();
    attrRemainingCol->SetReadOnly(true);
    attrRemainingCol->SetRenderer(moneyRenderer);
    gdEstimates->SetColAttr(EstimateColumn::REMAINING, attrRemainingCol);

    // OBS
    wxGridCellAttr* obsCol = new wxGridCellAttr();
    obsCol->SetReadOnly();
    gdEstimates->SetColAttr(EstimateColumn::OBS, obsCol);
}

void OrcamentoMainFrame::RefreshModel()
{
    lsMonths->Clear();
    try {
        _activeIndex = -1;
        _document->look(_budgetView, [this](int id, std::string name, int executing, int active) {
            wxString budgetName(wxString::FromUTF8(name.c_str()));
            if(not executing) {
                budgetName = "<em>" + budgetName + "</em>";
            } else if(active) {
                budgetName = "<strong>" + budgetName + "</strong>";
                _activeIndex = lsMonths->GetCount();
            }
            lsMonths->Append(budgetName, reinterpret_cast<void*>(id));
            if(active) {
                lsMonths->SetSelection(lsMonths->GetCount() - 1);
            }
        });
        if(!lsMonths->GetSelectedCount()){
            lsMonths->SetSelection(lsMonths->GetCount()-1);
        }
        RefreshEstimates();
    } catch(const std::exception& e) {
        wxMessageBox(e.what());
    }
}

void OrcamentoMainFrame::RefreshEstimates()
{
    if(gdEstimates->GetNumberRows()) {
        gdEstimates->DeleteRows(0, gdEstimates->GetNumberRows());
    }
    int budget_id = selectedBudgetId();
    if(budget_id <= 0) {
        return;
    }
    try {
        int i = 0;
        auto refreshFunction = [this, &i](int id,
                                          std::string name,
                                          std::string due,
                                          double estimated,
                                          double accounted,
                                          double remaining,
                                          std::string category,
                                          std::string obs) {
            gdEstimates->AppendRows();
            gdEstimates->SetCellValue(i, EstimateColumn::ID, wxString::FromDouble(id));
            gdEstimates->SetCellValue(i, EstimateColumn::NAME, wxString::FromUTF8(name.c_str()));
            gdEstimates->SetCellValue(i, EstimateColumn::DUE, wxString::FromUTF8(due.c_str()));
            gdEstimates->SetCellValue(i, EstimateColumn::ESTIMATED, wxString::FromDouble(estimated));
            gdEstimates->SetCellValue(i, EstimateColumn::ACCOUNTED, wxString::FromDouble(accounted));
            gdEstimates->SetCellValue(i, EstimateColumn::REMAINING, wxString::FromDouble(remaining));
            gdEstimates->SetCellValue(i, EstimateColumn::CATEGORY, wxString::FromUTF8(category.c_str()));
            gdEstimates->SetCellValue(i, EstimateColumn::OBS, wxString::FromUTF8(obs.c_str()));

            if(category == "") {
                attrNothing->IncRef();
                gdEstimates->SetRowAttr(i, attrNothing);
            } else {
                RefreshColorEstimate(i, estimated, accounted);
            }
            ++i;
        };
        if(lsMonths->GetSelection() > _activeIndex) {
            using namespace std::placeholders;
            _estimatePlaningView.budgetId(budget_id);
            int sort = _sort;
            if(sort > 5){
                sort -= 2;
            }else if(sort > 2){
                sort = 5;
                gdEstimates->SetSortingColumn(EstimateColumn::CATEGORY);
            }
            _estimatePlaningView.search(_search, sort, _asc);
            _document->look(_estimatePlaningView, std::bind(refreshFunction, _1, _2, _3, _4, 0, 0, _5, _6));
            RefreshCellAttr(false);
        } else {
            _estimateExecutingView.budgetId(budget_id);
            _estimateExecutingView.search(_search, _sort, _asc);
            _document->look(_estimateExecutingView, refreshFunction);
            RefreshCellAttr(true);
        }
        RefreshTotals();
    } catch(const std::exception& e) {
        wxMessageBox(e.what());
    }
}

void OrcamentoMainFrame::RefreshColorEstimate(int i, double estimated, double accounted)
{
    wxColour red{ 0xAAAAFF }, green{ 0xAAFFAA }, yellow{ 0xA9FEFF }, blue{ 0xFFD4AA }, purple{ 0xFFA9FF },//FFD3A9
        orange{ 0xAAD4FF }, white{ 0xFFFFFF };
    auto n_cols = gdEstimates->GetNumberCols();
    wxColour basic, current;
    if(estimated > 0){//credit
        basic = blue;
        double ratio = accounted/estimated;
        if(ratio >= 1){
            current = green;
        }else if(ratio >= 0){
            double factor = 1 - ratio;
            current = wxColour(basic.Red()+(255-basic.Red())*factor, 
                                basic.Green()+(255-basic.Green())*factor, 
                                basic.Blue()+(255-basic.Blue())*factor);
        } else {
            current = red;
        }
    } else if(estimated < 0){//debit
        basic = orange;
        double ratio = accounted/estimated;
        if(ratio > 1){
            current = red;
        }else if(ratio > 0){
            double factor = 1 - ratio;
            current = wxColour(basic.Red()+(255-basic.Red())*factor, 
                                basic.Green()+(255-basic.Green())*factor, 
                                basic.Blue()+(255-basic.Blue())*factor);
        } else {
            current = white;
        }
    }else {//Neutral
        basic = white;
        if(accounted > 0){
            current = green;
        }else if(accounted < 0){
            current = red;
        }else {
            current = white; 
        }
    }
    for(int j =0; j < n_cols; ++j){
        gdEstimates->SetCellBackgroundColour(i, j, basic);
    }
    gdEstimates->SetCellBackgroundColour(i, EstimateColumn::ACCOUNTED, current);
    gdEstimates->SetCellBackgroundColour(i, EstimateColumn::REMAINING, current);
}

void OrcamentoMainFrame::RefreshTotals()
{
    int budget_id = selectedBudgetId();
    if(budget_id < 1) {
        return;
    }
    try {
        _totalsView.budgetId(budget_id);
        _totalsView.search(_search);
        bool ok = false;
        _document->look(_totalsView,
                        [this, &ok](const std::string& budget, double estimated, double accounted, double remaining) {
//            sbStatus->SetStatusText(_("Budget: ") + wxString::FromUTF8(budget.c_str()), 1);
            txTotalEstimated->SetValue( wxString::FromDouble(estimated, 2));
            txTotalAccounted->SetValue(wxString::FromDouble(accounted, 2));
            txTotalRemaining->SetValue(wxString::FromDouble(remaining, 2));
            ok = true;
        });
        if(!ok) {
            wxMessageBox("Database inconsitence. Do you have a backup?");
        }
    } catch(const std::exception& e) {
        wxMessageBox(e.what());
    }
}

void OrcamentoMainFrame::RefreshCellAttr(bool executing)
{
    // Category
    wxGridCellAttr* attrCategoryCol = new wxGridCellAttr();
    wxArrayString choices;
    _document->look(_categoryView,
                    [this, &choices](int, const std::string& name) { choices.Add(wxString::FromUTF8(name.c_str())); });
    attrCategoryCol->SetEditor(new wxGridCellChoiceEditor(choices));
    gdEstimates->SetColAttr(EstimateColumn::CATEGORY, attrCategoryCol);

    // Show the execution only if possible.
    if(executing) {
        gdEstimates->ShowCol(EstimateColumn::ACCOUNTED);
        gdEstimates->ShowCol(EstimateColumn::REMAINING);
    } else {
        gdEstimates->HideCol(EstimateColumn::ACCOUNTED);
        gdEstimates->HideCol(EstimateColumn::REMAINING);
    }
}
void OrcamentoMainFrame::OnMnbudgetcreatenextMenuSelected(wxCommandEvent& event)
{
    if(wxMessageBox(L"Are you sure you want to create a new Budget?\nThis is irreversible and will cause the file to be saved.", L"Create Budget", wxYES_NO|wxCENTRE) != wxYES) {
        return;
    }
    try {
        _document->exec<action::InsertBudget>();
        _document->save();
    } catch (const std::exception &e) {
        wxMessageBox(e.what());
    }
    RefreshModel();
}
void OrcamentoMainFrame::OnMnbudgetexecutenextMenuSelected(wxCommandEvent& event)
{
    if(wxMessageBox(L"Are you sure you want to execute the next Budget?\nThis is irreversible and will cause the file to be saved.", L"Execute Budget", wxYES_NO|wxCENTRE) != wxYES) {
        return;
    }
    try {
        _document->exec<action::ExecuteNextBudget>();
        _document->save();
    } catch (const std::exception &e) {
        wxMessageBox(e.what());
    }
    RefreshModel();
}
void OrcamentoMainFrame::OnMnbudgetexportMenuSelected(wxCommandEvent& event)
{
    int budget_id = selectedBudgetId();
    if(budget_id <= 0) {
        return;
    }
    wxFileDialog openFileDialog(this, L"Select the location to export", "", "", "CSV(*.csv)|*.csv", wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if(openFileDialog.ShowModal() != wxID_OK) {
        return;
    }
    wxString location = openFileDialog.GetPath();

    using namespace jay::util;
    CSVwrite writter(location.ToStdString());
    writter.WriteUTF8BOM();
    writter.WriteRecord({"id", "name", "estimated", "accounted", "remaining", "category", "Obs"});
    for(int i=0; i < gdEstimates->GetNumberRows();++i){
        writter.WriteRecord({
            (std::string)gdEstimates->GetCellValue(i, EstimateColumn::ID).ToUTF8(),
            (std::string)gdEstimates->GetCellValue(i, EstimateColumn::NAME).ToUTF8(),
            (std::string)gdEstimates->GetCellValue(i, EstimateColumn::DUE).ToUTF8(),
            (std::string)gdEstimates->GetCellValue(i, EstimateColumn::ESTIMATED).ToUTF8(),
            (std::string)gdEstimates->GetCellValue(i, EstimateColumn::ACCOUNTED).ToUTF8(),
            (std::string)gdEstimates->GetCellValue(i, EstimateColumn::CATEGORY).ToUTF8(),
            (std::string)gdEstimates->GetCellValue(i, EstimateColumn::OBS).ToUTF8(),
        });
    }
}
void OrcamentoMainFrame::OnMnbudgetseparatorMenuSelected(wxCommandEvent& event)
{
}
void OrcamentoMainFrame::OnMnestimateaddMenuSelected(wxCommandEvent& event)
{
    int selection = selectedBudgetId();
    if(!selection) {
        return;
    }
    try {
        int newEstimate = _document->exec<action::InsertEstimate>(selection);
        gdEstimates->AppendRows();
        int newRow = gdEstimates->GetNumberRows()-1;
        gdEstimates->SetCellValue(newRow, 0, wxString::FromDouble(newEstimate));
    } catch (const std::exception &e) {
        wxMessageBox(e.what());
    }
}
void OrcamentoMainFrame::OnMnfilenewMenuSelected(wxCommandEvent& event)
{
    CreateDatabaseDialog dialog(this);
DIALOG_SHOW: //Don't do this at home, kids.
    if(dialog.ShowModal() != wxID_OK ) {
        return;
    }
    wxString location = dialog.getLocation();
    if(location.Trim().length()==0) {
        wxMessageBox("Invalid Path");
        goto DIALOG_SHOW;
    }
    wxDateTime start  = dialog.getStart();

    try {
        _document = OrcaDocument::create(location, start);
    } catch (const std::exception &e) {
        wxMessageBox(e.what());
        _document = nullptr;
        goto DIALOG_SHOW;
    }
    RefreshModel();
}

void OrcamentoMainFrame::OnMnfileopenMenuSelected(wxCommandEvent& event)
{
    wxFileDialog openFileDialog(
        this, _("Select the file"), "", "", "Orca files (*.orca)|*.orca", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if(openFileDialog.ShowModal() != wxID_OK) {
        return;
    }
    wxString location = openFileDialog.GetPath();
    try {
        _document = OrcaDocument::load(location);
        RefreshModel();
    } catch(const wrongver_error& e) {
        if(OrcaDocument::canConvert(e.major, e.minor, e.patch, e.variant)) {
            try {
                wxString backupLocation = location + ".old";
                if(rename(location, backupLocation)!=0){
                    std::runtime_error("Can not backup.");
                }
                _document = OrcaDocument::convertFrom0_1_0(backupLocation, location);
                RefreshModel();
            } catch(const std::exception& e) {
                wxMessageBox(e.what());
            }
        } else {
            wxMessageBox(e.what());
        }
    } catch(const std::exception& e) {
        wxMessageBox(e.what());
    }
}
void OrcamentoMainFrame::OnMnfilequitMenuSelected(wxCommandEvent& event)
{
    Close();
}
void OrcamentoMainFrame::OnMnfilesaveMenuSelected(wxCommandEvent& event)
{
    _document->save();
}
void OrcamentoMainFrame::OnMnfilesaveasMenuSelected(wxCommandEvent& event)
{
}
void OrcamentoMainFrame::OnMnhelpaboutMenuSelected(wxCommandEvent& event)
{
    wxString msg = wxbuildinfo(long_f);
    wxAboutDialogInfo info;
    info.SetName(_("OrcaMento"));
    info.SetVersion(_("0.4.1 Alpha"));
//    info.SetVersion(_("Build ")+isoDate()+_("(")+msg+_(")"));
    info.SetDescription(_("A small program to manage finances. \n"));
    info.SetCopyright(_("(C) 2014-2016 TalesM (talesm.github.io, tales.miranda88@gmail.com)"));
    info.SetWebSite(_("https://github.com/TalesM/orcamento"));
    wxIcon FrameIcon;
    FrameIcon.CopyFromBitmap(wxBitmap(wxImage(_T("orca_1.ico"))));
    info.SetIcon(FrameIcon);

    wxString licenseString = "This program is licensed by the terms of GPL 3.\nSee the included <LICENSE.txt> for more detail.";
    info.SetLicence(licenseString);

    wxAboutBox(info);
}
void OrcamentoMainFrame::OnMnwalletoverviewMenuSelected(wxCommandEvent& event)
{
    if(wxMessageBox(_("This action will cause the file to be saved. Do you want to continue?"), L"Wallet Overview", wxYES_NO|wxCENTRE) != wxYES) {
        return;
    }
    try {
        _document->save();
        WalletOverviewDialog overview(this);
        overview.giveDatabase(_document);
        int response = overview.ShowModal();
        _document = overview.takeDatabase();
        if(response == wxOK){
            _document->save();
        } else {
            while(_document->undo());
        }
    } catch(std::exception &e) {
        wxMessageBox(e.what());
    }
}
void OrcamentoMainFrame::OnCloseWindow(wxCloseEvent& event)
{
    if(!event.CanVeto() || !_document || !_document->dirt()) {
        Destroy();
    } else  {
        auto ans = wxMessageBox(_("Do you want to save your budget before exit?"),
                                _("OrcaMento"), wxYES_NO | wxCANCEL | wxCENTRE, this);
        switch(ans) {
        case wxYES:
            _document->save();
            Destroy();
            break;
        case wxNO:
            Destroy();
            break;
        case wxCANCEL:
            break;
        }
    }
}
void OrcamentoMainFrame::OnGdestimatesGridCellChanging(wxGridEvent& event)
{
    int row = event.GetRow(), col = event.GetCol();
    wxString newValue = event.GetString();
    if(event.GetString() == gdEstimates->GetCellValue(row, col)) { //The default editor duplicate events, so this avoids the first one.
        return;
    }
    long id;
    if(!gdEstimates->GetCellValue(row, EstimateColumn::ID).ToCLong(&id)) {
        wxMessageBox(L"Corrupted Row: '"+gdEstimates->GetColLabelValue(0)+"'");
        return;
    }
    int estimateId = int(id);
    try {
        switch(col) {
        case EstimateColumn::NAME:
            _document->exec<action::UpdateEstimateName>(estimateId, std::string(newValue.ToUTF8()));
            break;
        case EstimateColumn::ESTIMATED:{
            double newEstimated = atof(newValue);
            _document->exec<action::UpdateEstimateAmount>(estimateId, newEstimated);
            RefreshTotals();
            RefreshColorEstimate(row, newEstimated, atof(gdEstimates->GetCellValue(row, EstimateColumn::ACCOUNTED)));
            break;
        }
        case EstimateColumn::DUE:
            if(newValue.length()) {
                int day = atoi(newValue) - 1;
                _document->exec<action::UpdateEstimateDue>(estimateId, day);
            } else {
                _document->exec<action::DeleteEstimateDue>(estimateId);
            }
            break;
        case EstimateColumn::CATEGORY:
            _document->exec<action::UpdateEstimateCategory>(estimateId, std::string(newValue.ToUTF8()));
            break;
        default:
            break;
        }
    } catch (const std::exception &e) {
        wxMessageBox(e.what());
    }
}
void OrcamentoMainFrame::OnGdestimatesGridCellLeftDclick(wxGridEvent& event)
{
    int row = event.GetRow(), col = event.GetCol();
    if(col == EstimateColumn::ACCOUNTED) {
        ExecutionDialog executionDialog(this, wxID_ANY, atoi(gdEstimates->GetCellValue(row, 0)));
        executionDialog.giveDatabase(_document);
        executionDialog.ShowModal();
        _document = executionDialog.takeDatabase();
        RefreshEstimates();
    } else if(col ==EstimateColumn::OBS) {
        wxTextEntryDialog obsDialog(this, L"Write an Observation for \""+gdEstimates->GetCellValue(row, EstimateColumn::NAME)+L"\"",
                                    L"OBS", gdEstimates->GetCellValue(row, EstimateColumn::OBS), wxTE_MULTILINE|wxTextEntryDialogStyle);
        if(obsDialog.ShowModal() != wxID_OK) {
            return;
        }
        try {
            int id = atoi(gdEstimates->GetCellValue(row, EstimateColumn::ID));
            auto val = obsDialog.GetValue();
            _document->exec<action::UpdateEstimateObs>(id, std::string(val.ToUTF8()));
            gdEstimates->SetCellValue(row, EstimateColumn::OBS, val);
        } catch (const std::exception &e) {
            wxMessageBox(e.what());
        }
    }
}
void OrcamentoMainFrame::OnGdestimatesGridCellRightClick(wxGridEvent& event)
{
    int row = event.GetRow();
    wxPoint point = event.GetPosition();
    cmEstimate->SetClientData(reinterpret_cast<void*>(row));
    PopupMenu( cmEstimate, gdEstimates->GetPosition() + point);
    cmEstimate->SetClientData(NULL);
}
 
void OrcamentoMainFrame::OnCmestimatescopyselectedrowsMenuSelected(wxCommandEvent& event)
{
    wxArrayInt selectedRows = gdEstimates->GetSelectedRows();
    if(selectedRows.size()==0) {
        wxMessageBox(L"You need to select at least an entire row.");
        return;
    }
    int budget_id = lsMonths->GetSelection()+1;
    if(budget_id <= 0) {
        return;
    }
    int increment = 0;
    try {
        _budgetCopyView.sourceBudgetId(budget_id);
        wxArrayString options;
        bool ok = false;
        _document->look(_budgetCopyView, [this, &options, &ok](const std::string &name) {
            options.push_back(wxString::FromUTF8(name.c_str()));
            ok = true;
        });
        if(!ok) {
            wxMessageBox(L"This is the last budget.");
            return;
        }
        increment = 1 + wxGetSingleChoiceIndex(L"Select the destiny Budget", "Select Budget", options, 0, this);
    } catch (const std::exception &e) {
        wxMessageBox(e.what());
    }
    if(increment <=0) {
        return;
    }
    for(int row: selectedRows) {
        int id = atoi(gdEstimates->GetCellValue(row, EstimateColumn::ID));
        try {
            _document->exec<action::CopySelectedEstimateTo>(id, increment);
        } catch (const std::exception &e) {
            // TODO (Tales#1#): Choose to continue or cancel.
            wxMessageBox(e.what());
        }
    }
}
void OrcamentoMainFrame::OnCmestimatesdeleteMenuSelected(wxCommandEvent& event)
{
    int row = reinterpret_cast<int>(cmEstimate->GetClientData());
    if(wxMessageBox(L"Are you sure you want to delete \""
                    +gdEstimates->GetCellValue(row, EstimateColumn::NAME)+"\"",
                    "Delete Confirmation", wxOK|wxCENTRE|wxCANCEL, this) != wxOK
      ) {
        return;
    }
    try {
        int estimateId = atoi(gdEstimates->GetCellValue(row, EstimateColumn::ID));
        _document->exec<action::DeleteEstimate>(estimateId);
        RefreshEstimates();
    } catch(std::exception &e) {
        wxMessageBox(e.what());
    }
}
void OrcamentoMainFrame::OnCmestimatesexecuteMenuSelected(wxCommandEvent& event)
{
    int row = reinterpret_cast<int>(cmEstimate->GetClientData());
    ExecutionDialog executionDialog(this, wxID_ANY, atoi(gdEstimates->GetCellValue(row, 0)));
    executionDialog.giveDatabase(_document);
    executionDialog.ShowModal();
    _document = executionDialog.takeDatabase();
    RefreshEstimates();
}
void OrcamentoMainFrame::OnLsmonthsListbox(wxCommandEvent& event)
{
    RefreshEstimates();
}
void OrcamentoMainFrame::OnByfiltertotalsButtonClicked(wxCommandEvent& event)
{
    if(!dgFilter){
        dgFilter = new BudgetFilter(this);
        dgFilter->addSearchListerner([this](const Search &s){
            _search = s;
            RefreshEstimates();
        });
    }
    dgFilter->refreshFields(*_document);
    dgFilter->Show();
}

void OrcamentoMainFrame::OnGdestimatesGridColSort(wxGridEvent& event)
{
    _sort = event.GetCol();
    if(gdEstimates->GetSortingColumn() == _sort){
        _asc = !_asc;
    } else {
        _asc = true;
    }
    RefreshEstimates();
}
void OrcamentoMainFrame::OnMneditredoMenuSelected(wxCommandEvent& event)
{
    _document->redo();
    RefreshEstimates();
}
void OrcamentoMainFrame::OnMnfileundoMenuSelected(wxCommandEvent& event)
{
    _document->undo();
    RefreshEstimates();
}
