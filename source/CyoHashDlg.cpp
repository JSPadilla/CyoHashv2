//////////////////////////////////////////////////////////////////////
// CyoHashDlg.cpp - part of the CyoHash application
//
// Copyright (c) Graham Bull. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "CyoHashDlg.h"
#include "Hasher.h"
#include "ForensicReportDlg.h"
#include "PdfReport.h"

//////////////////////////////////////////////////////////////////////
// Construction

CyoHashDlg::CyoHashDlg( LPCWSTR pathname, LPCWSTR algorithm, LPCWSTR hash, bool splitHash, ULONGLONG fileSize, const FILETIME& hashStartedUtc, const FILETIME& hashedUtc )
:   m_hIcon( NULL ),
    m_pathname( pathname ),
    m_algorithm( algorithm ),
    m_hash( hash ),
    m_splitHash( splitHash ),
    m_fileSize( fileSize ),
    m_hashStartedUtc( hashStartedUtc ),
    m_hashedUtc( hashedUtc ),
    m_bEmpty( true ),
    m_bValid( false )
{
}

CyoHashDlg::~CyoHashDlg()
{
    if (m_hIcon != NULL)
        ::DestroyIcon( m_hIcon );
}


void CyoHashDlg::OnFinalMessage( HWND hWnd )
{
    delete this;
}

//////////////////////////////////////////////////////////////////////
// Message Handlers

LRESULT CyoHashDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    CAxDialogImpl< CyoHashDlg >::OnInitDialog( uMsg, wParam, lParam, bHandled );

    ::SetForegroundWindow( m_hWnd );
    SetWindowPos( HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );

    m_hIcon = (HICON)::LoadImage( ::GetModuleHandle( NULL ), MAKEINTRESOURCE( IDI_CYOHASH ), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE );
    SetIcon( m_hIcon, TRUE );
    SetIcon( m_hIcon, FALSE );

    SetDlgItemText( IDC_FILE, m_pathname );

    CStrBufW pathname( m_pathname );
    CStringW filename = ::PathFindFileNameW( pathname );
    SetWindowTextW( CStringW( L"CyoHash - " ) + filename );

    CStringW hashname;
    hashname.Format( L"The %s is:", m_algorithm );
    SetDlgItemTextW( IDC_HASHNAME, hashname );
    CStringW hash = m_hash;
    if (m_splitHash)
        hash.Insert( hash.GetLength() / 2, L"\r\n" );
    SetDlgItemTextW( IDC_HASH, (LPCWSTR)hash );

    SetDlgItemText( IDC_RESULT, L"Paste:" );

    bHandled = TRUE;
    return 1;
}

LRESULT CyoHashDlg::OnClose( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    DestroyWindow();

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg::OnClickedOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    DestroyWindow();

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg::OnClickedCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    DestroyWindow();

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg::OnEnChangeHashValidate( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    CWindow wnd = GetDlgItem( IDC_HASH_VALIDATE );

    CStringW hashValidate;
    wnd.GetWindowText( hashValidate );
    hashValidate = hashValidate.MakeUpper().Trim();

    CStringW hash = m_hash.MakeUpper().Trim();

    m_bEmpty = hashValidate.IsEmpty();
    m_bValid = (hash == hashValidate);
    if (m_bEmpty)
        SetDlgItemText( IDC_RESULT, _T("Paste:") );
    else if (m_bValid)
        SetDlgItemText( IDC_RESULT, _T("Valid") );
    else
        SetDlgItemText( IDC_RESULT, _T("NOT valid") );
    wnd.Invalidate();

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg::OnCtlColorEdit( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    HWND hWnd = GetDlgItem( IDC_HASH_VALIDATE ).m_hWnd;
    if (hWnd == (HWND)lParam && !m_bEmpty)
    {
        SetBkColor( (HDC)wParam, m_bValid ? RGB( 96, 255, 96 ) : RGB( 255, 96, 96 ));
        bHandled = TRUE;
        return 1;
    }

    bHandled = TRUE;
    return 0;
}


LRESULT CyoHashDlg::OnClickedExportPdf( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    ForensicReportDlg details;
    if (details.DoModal( m_hWnd ) != IDOK)
    {
        bHandled = TRUE;
        return 0;
    }

    CComPtr<IFileDialog> fileDialog;
    if (FAILED(fileDialog.CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER)))
    {
        ::MessageBoxW(m_hWnd, L"Unable to open the Save PDF dialog.", L"CyoHash - Forensic Report", MB_OK | MB_ICONERROR);
        bHandled = TRUE;
        return 0;
    }
    COMDLG_FILTERSPEC filters[] = { { L"PDF files", L"*.pdf" }, { L"All files", L"*.*" } };
    fileDialog->SetFileTypes(ARRAYSIZE(filters), filters);
    fileDialog->SetFileTypeIndex(1);
    fileDialog->SetDefaultExtension(L"pdf");

    CStringW filename = ::PathFindFileNameW((LPCWSTR)m_pathname);
    int dot = filename.ReverseFind(L'.');
    if (dot > 0) filename = filename.Left(dot);
    CStringW suggested = filename + L"_Hash_Report.pdf";
    fileDialog->SetFileName(suggested);

    if (SUCCEEDED(fileDialog->Show(m_hWnd)))
    {
        CComPtr<IShellItem> item;
        if (SUCCEEDED(fileDialog->GetResult(&item)))
        {
            PWSTR path = NULL;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                ForensicHashReportData data;
                data.caseName = details.caseName;
                data.examiner = details.examiner;
                data.evidenceItem = details.evidenceItem;
                data.pathname = m_pathname;
                data.algorithm = m_algorithm;
                data.hash = m_hash;
                data.fileSize = m_fileSize;
                data.hashStartedUtc = m_hashStartedUtc;
                data.hashedUtc = m_hashedUtc;
                data.suppressUserProfile = details.suppressUserProfile;
                CStringW error;
                if (pdfreport::SaveForensicHashReport(path, data, error))
                    ::MessageBoxW(m_hWnd, L"Forensic hash report exported successfully.", L"CyoHash - Forensic Report", MB_OK | MB_ICONINFORMATION);
                else
                    ::MessageBoxW(m_hWnd, error, L"CyoHash - Forensic Report", MB_OK | MB_ICONERROR);
                ::CoTaskMemFree(path);
            }
        }
    }

    bHandled = TRUE;
    return 0;
}
