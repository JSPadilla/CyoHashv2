#pragma once

#include "Resource.h"

class ForensicReportDlg : public CAxDialogImpl< ForensicReportDlg >
{
public:
    enum class PdfSortField
    {
        None = 0,
        FileName,
        FileSize,
        Algorithm,
        PathName,
        HashValue
    };

    struct PdfSortLevel
    {
        PdfSortField field;
        bool ascending;

        PdfSortLevel()
            : field(PdfSortField::None), ascending(true)
        {
        }
    };

    ForensicReportDlg();
    enum { IDD = IDD_FORENSIC_REPORT };

BEGIN_MSG_MAP( ForensicReportDlg )
    MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
    COMMAND_HANDLER( IDOK, BN_CLICKED, OnClickedOK )
    COMMAND_HANDLER( IDCANCEL, BN_CLICKED, OnClickedCancel )
    CHAIN_MSG_MAP( CAxDialogImpl< ForensicReportDlg >)
END_MSG_MAP()

    LRESULT OnInitDialog( UINT, WPARAM, LPARAM, BOOL& );
    LRESULT OnClickedOK( WORD, WORD, HWND, BOOL& );
    LRESULT OnClickedCancel( WORD, WORD, HWND, BOOL& );

    CStringW caseName;
    CStringW examiner;
    CStringW evidenceItem;
    bool suppressUserProfile;
    PdfSortLevel sortLevels[5];

private:
    void InitialiseSortFieldCombo( int controlId, PdfSortField defaultField );
    void InitialiseSortDirectionCombo( int controlId );
    PdfSortField ReadSortField( int controlId ) const;
    bool ReadSortAscending( int controlId ) const;
};
