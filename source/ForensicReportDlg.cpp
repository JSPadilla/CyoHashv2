#include "stdafx.h"
#include "ForensicReportDlg.h"

ForensicReportDlg::ForensicReportDlg()
    : suppressUserProfile(false)
{
}

void ForensicReportDlg::InitialiseSortFieldCombo( int controlId, PdfSortField defaultField )
{
    HWND combo = GetDlgItem( controlId );
    ::SendMessageW( combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None") );
    ::SendMessageW( combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"File Name") );
    ::SendMessageW( combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"File Size") );
    ::SendMessageW( combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Algorithm") );
    ::SendMessageW( combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Full Path Name") );
    ::SendMessageW( combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Hash Value") );
    ::SendMessageW( combo, CB_SETCURSEL, static_cast<WPARAM>(defaultField), 0 );
}

void ForensicReportDlg::InitialiseSortDirectionCombo( int controlId )
{
    HWND combo = GetDlgItem( controlId );
    ::SendMessageW( combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ascending") );
    ::SendMessageW( combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Descending") );
    ::SendMessageW( combo, CB_SETCURSEL, 0, 0 );
}

ForensicReportDlg::PdfSortField ForensicReportDlg::ReadSortField( int controlId ) const
{
    int selection = static_cast<int>(::SendMessageW( GetDlgItem( controlId ), CB_GETCURSEL, 0, 0 ));
    if (selection < 0 || selection > static_cast<int>(PdfSortField::HashValue))
        return PdfSortField::None;
    return static_cast<PdfSortField>(selection);
}

bool ForensicReportDlg::ReadSortAscending( int controlId ) const
{
    int selection = static_cast<int>(::SendMessageW( GetDlgItem( controlId ), CB_GETCURSEL, 0, 0 ));
    return selection != 1;
}

LRESULT ForensicReportDlg::OnInitDialog( UINT, WPARAM, LPARAM, BOOL& bHandled )
{
    SetWindowTextW( L"CyoHash - Forensic Report Details" );

    // Default to a predictable forensic-report order while allowing the user
    // to choose any combination of the available PDF sort fields.
    InitialiseSortFieldCombo( IDC_SORT_FIELD_1, PdfSortField::FileName );
    InitialiseSortFieldCombo( IDC_SORT_FIELD_2, PdfSortField::None );
    InitialiseSortFieldCombo( IDC_SORT_FIELD_3, PdfSortField::None );
    InitialiseSortFieldCombo( IDC_SORT_FIELD_4, PdfSortField::None );
    InitialiseSortFieldCombo( IDC_SORT_FIELD_5, PdfSortField::None );
    InitialiseSortDirectionCombo( IDC_SORT_DIR_1 );
    InitialiseSortDirectionCombo( IDC_SORT_DIR_2 );
    InitialiseSortDirectionCombo( IDC_SORT_DIR_3 );
    InitialiseSortDirectionCombo( IDC_SORT_DIR_4 );
    InitialiseSortDirectionCombo( IDC_SORT_DIR_5 );

    CenterWindow( GetParent() );
    GetDlgItem( IDC_CASE_NAME ).SetFocus();
    bHandled = TRUE;
    return FALSE;
}

LRESULT ForensicReportDlg::OnClickedOK( WORD, WORD, HWND, BOOL& bHandled )
{
    GetDlgItemTextW( IDC_CASE_NAME, caseName );
    GetDlgItemTextW( IDC_EXAMINER, examiner );
    GetDlgItemTextW( IDC_EVIDENCE_ITEM, evidenceItem );
    suppressUserProfile = (IsDlgButtonChecked( IDC_SUPPRESS_USERPROFILE ) == BST_CHECKED);
    caseName.Trim();
    examiner.Trim();
    evidenceItem.Trim();

    if (caseName.IsEmpty() || examiner.IsEmpty())
    {
        ::MessageBoxW( m_hWnd, L"Case Number/Name and Examiner are required for a forensic report.",
            L"CyoHash - Forensic Report", MB_OK | MB_ICONINFORMATION );
        bHandled = TRUE;
        return 0;
    }

    sortLevels[0].field = ReadSortField( IDC_SORT_FIELD_1 );
    sortLevels[1].field = ReadSortField( IDC_SORT_FIELD_2 );
    sortLevels[2].field = ReadSortField( IDC_SORT_FIELD_3 );
    sortLevels[3].field = ReadSortField( IDC_SORT_FIELD_4 );
    sortLevels[4].field = ReadSortField( IDC_SORT_FIELD_5 );
    sortLevels[0].ascending = ReadSortAscending( IDC_SORT_DIR_1 );
    sortLevels[1].ascending = ReadSortAscending( IDC_SORT_DIR_2 );
    sortLevels[2].ascending = ReadSortAscending( IDC_SORT_DIR_3 );
    sortLevels[3].ascending = ReadSortAscending( IDC_SORT_DIR_4 );
    sortLevels[4].ascending = ReadSortAscending( IDC_SORT_DIR_5 );

    // Using the same field at multiple levels adds no useful ordering and can
    // make the export settings misleading, so reject duplicate active fields.
    for (int i = 0; i < 5; ++i)
    {
        if (sortLevels[i].field == PdfSortField::None)
            continue;
        for (int j = i + 1; j < 5; ++j)
        {
            if (sortLevels[i].field == sortLevels[j].field)
            {
                ::MessageBoxW( m_hWnd,
                    L"Each active PDF sort level must use a different column.",
                    L"CyoHash - Forensic Report", MB_OK | MB_ICONINFORMATION );
                bHandled = TRUE;
                return 0;
            }
        }
    }

    EndDialog( IDOK );
    bHandled = TRUE;
    return 0;
}

LRESULT ForensicReportDlg::OnClickedCancel( WORD, WORD, HWND, BOOL& bHandled )
{
    EndDialog( IDCANCEL );
    bHandled = TRUE;
    return 0;
}
