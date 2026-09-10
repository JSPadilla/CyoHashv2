//////////////////////////////////////////////////////////////////////
// CyoHashDlg2.cpp - part of the CyoHash application
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
#include "AboutDlg.h"
#include "CyoHashDlg.h"
#include "CyoHashDlg2.h"
#include "BrowseDlg.h"
#include "Hasher.h"
#include "NamedPipeServer.h"
#include "ForensicReportDlg.h"
#include "PdfReport.h"
#include "Utils.h"
#include <algorithm>

//////////////////////////////////////////////////////////////////////
// Helpers/Constants

namespace
{
    const int SUBMENU_PAUSE = 0;
    const int SUBMENU_RESUME = 1;
    const int SUBMENU_PAUSERESUME = 2;
    const int SUBMENU_COMPLETED = 3;
    const int SUBMENU_CANCELLED = 4;
    const int SUBMENU_LIST = 5;
    const int SUBMENU_ALLHASHES = 6;

    class NamedPipeServerCallback : public INamedPipeServerCallback
    {
    public:
        NamedPipeServerCallback( CyoHashDlg2& dlg )
            : m_dlg( dlg )
        {
        }
        virtual void OnPipeReady()
        {
            m_dlg.OnPipeReady();
        }
        virtual void OnMessageReceived( LPBYTE start, DWORD size )
        {
            m_dlg.OnMessageReceived( start, size );
        }
    private:
        CyoHashDlg2& m_dlg;
    };
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction

CyoHashDlg2::CyoHashDlg2( LPCWSTR pipeName, HANDLE dialogReadyEvent, LPCWSTR pathname, LPCWSTR algorithm )
:   m_lastX(-1),
    m_lastY(-1),
    m_lastWidth(-1),
    m_lastHeight(-1),
    m_pipeName( pipeName ),
    m_dialogReadyEvent( dialogReadyEvent ),
    m_pipeThread( NULL ),
    m_exitEvent( NULL ),
    m_hIcon( NULL ),
    m_hMenu( NULL ),
    m_hMainMenu( NULL ),
    m_pathname( pathname ),
    m_algorithm( algorithm ),
    m_nextKey( 0 ),
    m_sortBy( SortBy::Unsorted ),
    m_alwaysOnTop( true )
{
    m_sync.Init();

    m_columnWidths[0] = -1;
    m_columnWidths[1] = -1;
    m_columnWidths[2] = -1;

    m_exitEvent = ::CreateEventW( NULL, TRUE, FALSE, NULL );
    ATLASSERT( m_exitEvent != NULL );

    m_pipeReadyEvent = ::CreateEventW( NULL, TRUE, FALSE, NULL );
    ATLASSERT( m_pipeReadyEvent != NULL );

    DWORD threadId;
    m_pipeThread = ::CreateThread( NULL, 0, &StaticPipeThread, this, CREATE_SUSPENDED, &threadId );
    ATLASSERT( m_pipeThread != NULL );
    ::ResumeThread( m_pipeThread );

    m_hIcon = (HICON)::LoadImage( ::GetModuleHandle( NULL ), MAKEINTRESOURCE( IDI_CYOHASH ), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE );
    ATLASSERT( m_hIcon != NULL );

    m_hMenu = ::LoadMenuW( _AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE( IDR_MENU1 ));
    ATLASSERT( m_hMenu != NULL );

    m_hMainMenu = ::LoadMenuW( _AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE( IDR_MAINMENU ));
    ATLASSERT( m_hMainMenu != NULL );
}

CyoHashDlg2::~CyoHashDlg2()
{
    if (m_hMainMenu != NULL)
    {
        ::SetMenu( m_hWnd, NULL );
        ::DestroyMenu( m_hMainMenu );
        m_hMainMenu = NULL;
    }

    if (m_hMenu != NULL)
    {
        ::DestroyMenu( m_hMenu );
        m_hMenu = NULL;
    }

    if (m_hIcon != NULL)
    {
        ::DestroyIcon( m_hIcon );
        m_hIcon = NULL;
    }

    if (m_exitEvent != NULL)
    {
        ::SetEvent( m_exitEvent );
        SafeJoinAll();
    }

    for (POSITION pos = m_hashData.GetStartPosition(); pos != NULL; )
    {
        HashMap::CPair* pair = m_hashData.GetNext( pos );
        ::SetEvent( pair->m_value.cancelEvent );
        if (::WaitForSingleObject( pair->m_value.thread, 5000 ) != WAIT_OBJECT_0)
        {
            //::OutputDebugStringW( L"ERROR: WaitForSingleObject failed\n" );
            //throw;
        }
        ::CloseHandle( pair->m_value.pauseEvent );
        ::CloseHandle( pair->m_value.resumeEvent );
        ::CloseHandle( pair->m_value.cancelEvent );
    }
    m_hashData.RemoveAll();

    if (m_pipeThread != NULL)
    {
        if (::WaitForSingleObject( m_pipeThread, 5000 ) != WAIT_OBJECT_0)
        {
            //::OutputDebugStringW( L"ERROR: WaitForSingleObject failed\n" );
            //throw;
        }
        ::CloseHandle( m_pipeThread );
        m_pipeThread = NULL;
    }

    if (m_pipeReadyEvent != NULL)
    {
        ::CloseHandle( m_pipeReadyEvent );
        m_pipeReadyEvent = NULL;
    }

    if (m_exitEvent != NULL)
    {
        ::CloseHandle( m_exitEvent );
        m_exitEvent = NULL;
    }

    m_sync.Term();
}

//////////////////////////////////////////////////////////////////////
// Message Handlers

LRESULT CyoHashDlg2::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    CAxDialogImpl< CyoHashDlg2 >::OnInitDialog( uMsg, wParam, lParam, bHandled );

    ReadLastSettings();

    SetWindowTextW( L"CyoHash v2.6.0" );
    if (m_hMainMenu != NULL)
        ::SetMenu( m_hWnd, m_hMainMenu );

    if (m_hMainMenu != NULL)
    {
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_ALWAYS_ON_TOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_UNSORTED, MF_BYCOMMAND | (m_sortBy == SortBy::Unsorted ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_FILE, MF_BYCOMMAND | (m_sortBy == SortBy::SortByFile ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_ALGORITHM, MF_BYCOMMAND | (m_sortBy == SortBy::SortByAlgorithm ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_HASH, MF_BYCOMMAND | (m_sortBy == SortBy::SortByHash ? MF_CHECKED : MF_UNCHECKED) );
    }

    InitList();

    if (m_lastX >= 0 && m_lastY >= 0 && m_lastWidth > 0 && m_lastHeight > 0)
        MoveWindow(m_lastX, m_lastY, m_lastWidth, m_lastHeight);
    ResizeList();

    BringWindowToTop();
    SetWindowPos(m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    SetIcon( m_hIcon, TRUE );
    SetIcon( m_hIcon, FALSE );

    ::DragAcceptFiles( m_hWnd, TRUE );

    if (::WaitForSingleObject( m_pipeReadyEvent, 30000 ) != WAIT_OBJECT_0)
    {
        //::OutputDebugStringW( L"ERROR: WaitForSingleObject failed\n" );
        throw;
    }

    if (!BeginHashing( m_pathname, m_algorithm ))
        EndDialog( IDCANCEL );

    ::SetEvent( m_dialogReadyEvent );

    bHandled = TRUE;
    return 1;
}

LRESULT CyoHashDlg2::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    ResizeList();

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnDropFiles( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    HDROP hDrop = (HDROP)wParam;

    // Get the pathnames for each selected file...
    m_droppedFiles.RemoveAll();
    UINT count = ::DragQueryFileW( hDrop, 0xFFFFFFFF, NULL, 0 );
    for (UINT i = 0; i < count; ++i)
    {
        wchar_t szPathname[ MAX_PATH ];
        ::DragQueryFileW( hDrop, i, szPathname, MAX_PATH );
        m_droppedFiles.AddTail( szPathname );
    }

    POINT point = { 0 };
    ::DragQueryPoint( hDrop, &point );
    ClientToScreen( &point );

    ::DragFinish( hDrop );

    HMENU hSubMenu = ::GetSubMenu( m_hMenu, SUBMENU_ALLHASHES );

    ATLASSERT( hSubMenu != NULL );
    ::TrackPopupMenu( hSubMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, point.x, point.y, 0, m_hWnd, NULL );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnClose( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    SaveCurrentSettings();

    ::SetEvent( m_exitEvent );
    SafeJoinAll();

    EndDialog( IDCANCEL );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnClickedCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    SaveCurrentSettings();

    ::SetEvent( m_exitEvent );
    SafeJoinAll();

    EndDialog( IDCANCEL );

    bHandled = TRUE;
    return 0;
}

//////////////////////////////////////////////////////////////////////
// Custom Messages

LRESULT CyoHashDlg2::OnHashingStarted( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    const int key = (int)wParam;

    HashData hashData = SafeGetHashData( key );

    LVITEM lvi = { 0 };
    lvi.mask = (LVIF_TEXT | LVIF_PARAM);
    lvi.iItem = INT_MAX;
    lvi.pszText = (LPWSTR)(LPCWSTR)hashData.pathname;
    lvi.lParam = key;
    int item = ListView_InsertItem( m_listWnd, &lvi );

    ListView_SetItemText( m_listWnd, item, 1, (LPWSTR)(LPCWSTR)hashData.algorithm );

    SortList();

    bHandled = TRUE;
    return 0;
}

void CyoHashDlg2::SortList()
{
    ListView_SortItems(m_listWnd, StaticCompareFunc, (LPARAM)this);
}

int CALLBACK CyoHashDlg2::StaticCompareFunc( LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort )
{
    CyoHashDlg2* pDlg = (CyoHashDlg2*)lParamSort;
    return pDlg->CompareFunc( lParam1, lParam2 );
}

int CyoHashDlg2::CompareFunc(LPARAM lParam1, LPARAM lParam2)
{
    int key1 = (int)lParam1;
    int key2 = (int)lParam2;

    if (m_sortBy == SortBy::Unsorted)
    {
        if (key1 < key2)
            return -1;
        else if (key1 > key2)
            return 1;
        else
            return 0;
    }

    int item1 = FindItem( key1 );
    int item2 = FindItem( key2 );
    int subitem;
    switch (m_sortBy)
    {
    case SortBy::SortByFile: subitem = 0; break;
    case SortBy::SortByAlgorithm: subitem = 1; break;
    case SortBy::SortByHash: subitem = 2; break;
    default: throw;
    }
    wchar_t szText1[ 256 ];
    wchar_t szText2[ 256 ];
    ListView_GetItemText( m_listWnd, item1, subitem, szText1, sizeof( szText1 ));
    ListView_GetItemText( m_listWnd, item2, subitem, szText2, sizeof( szText2 ));
    return _wcsicmp( szText1, szText2 );
}

LRESULT CyoHashDlg2::OnHashingProgress(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    const int key = (int)wParam;
    const int percentage = (int)lParam;

    int item = FindItem( key );

    wchar_t szProgress[ 50 ];
    swprintf_s( szProgress, L"Calculating (%d%%)", percentage );

    ListView_SetItemText( m_listWnd, item, 2, szProgress );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnHashingPaused( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    const int key = (int)wParam;
    const int percentage = (int)lParam;

    int item = FindItem( key );

    wchar_t szProgress[ 50 ];
    swprintf_s( szProgress, L"Paused (%d%%)", percentage );

    ListView_SetItemText( m_listWnd, item, 2, szProgress );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnHashingCompleted( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    const int key = (int)wParam;

    HashData hashData = SafeGetHashData( key );
    int item = FindItem( key );

    ListView_SetItemText( m_listWnd, item, 2, (LPWSTR)(LPCWSTR)hashData.hash );

    SortList();
    ::InvalidateRect( m_listWnd, NULL, FALSE );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnHashingCancelled( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
    const int key = (int)wParam;

    int item = FindItem( key );

    ListView_SetItemText( m_listWnd, item, 2, L"Cancelled" );

    bHandled = TRUE;
    return 0;
}

//////////////////////////////////////////////////////////////////////
// List Handlers

LRESULT CyoHashDlg2::OnDblClkList( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
    NMITEMACTIVATE* nmItem = (NMITEMACTIVATE*)pnmh;
    ATLASSERT( nmItem != NULL );

    if (nmItem->iItem >= 0)
    {
        int key = GetItemKey( nmItem->iItem );
        HashData hashData = SafeGetHashData( key );

        if (hashData.completed)
        {
            CyoHashDlg* dlg = new CyoHashDlg( hashData.pathname, hashData.algorithmLong, hashData.hash, hashData.splitHash, hashData.fileSize, hashData.hashStartedUtc, hashData.hashedUtc );
            if (dlg->Create( m_hWnd ) == NULL)
            {
                delete dlg;
                ::MessageBoxW( m_hWnd, L"Unable to open the hash detail window.", L"CyoHash", MB_OK | MB_ICONERROR );
            }
            else
            {
                dlg->ShowWindow( SW_SHOWNORMAL );
                ::SetForegroundWindow( dlg->m_hWnd );
            }
        }
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnRClickList( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
    NMITEMACTIVATE* nmItem = (NMITEMACTIVATE*)pnmh;
    ATLASSERT( nmItem != NULL );

    RECT rect = { 0 };
    m_listWnd.GetWindowRect( &rect );
    int x = (rect.left + nmItem->ptAction.x);
    int y = (rect.top + nmItem->ptAction.y);

    ShowPopupMenu( x, y );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnKeyDownList( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
    NMLVKEYDOWN* nm = (NMLVKEYDOWN*)pnmh;

    switch (nm->wVKey)
    {
    case VK_DELETE:
        ClearSelectedItems();
        bHandled = TRUE;
        return 0;

    case 'A':
        {
            if (((int)::GetAsyncKeyState( VK_CONTROL ) & 0x8000) == 0)
                break;
            int items = ListView_GetItemCount( m_listWnd );
            for (int i = 0; i < items; ++i)
                ListView_SetItemState( m_listWnd, i, LVIS_SELECTED, LVIS_SELECTED );
            bHandled = TRUE;
            return 0;
        }

    case VK_F10:
        if (((int)::GetAsyncKeyState( VK_SHIFT ) & 0x8000) == 0)
            break;
    case VK_APPS:
        {
            RECT rectWnd = { 0 };
            ::GetWindowRect( m_listWnd, &rectWnd );
            int x = rectWnd.left;
            int y = rectWnd.top;

            ShowPopupMenu( x, y );

            bHandled = TRUE;
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CyoHashDlg2::OnCustomDrawList( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
    NMLVCUSTOMDRAW* customDraw = reinterpret_cast<NMLVCUSTOMDRAW*>( pnmh );
    if (customDraw == NULL)
    {
        bHandled = FALSE;
        return CDRF_DODEFAULT;
    }

    if (customDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
    {
        bHandled = TRUE;
        return CDRF_NOTIFYITEMDRAW;
    }

    if (customDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
    {
        int item = static_cast<int>( customDraw->nmcd.dwItemSpec );
        if (item >= 0)
        {
            int key = GetItemKey( item );
            const bool selected = ((customDraw->nmcd.uItemState & CDIS_SELECTED) != 0);
            if (selected && IsMatchingHighlightKey( key ))
            {
                // Keep matching-hash selection visually distinct from normal Windows selection.
                customDraw->nmcd.uItemState &= ~CDIS_SELECTED;
                customDraw->clrTextBk = RGB( 198, 239, 206 );
                customDraw->clrText = RGB( 0, 97, 0 );
            }
        }
        bHandled = TRUE;
        return CDRF_DODEFAULT;
    }

    bHandled = FALSE;
    return CDRF_DODEFAULT;
}

//////////////////////////////////////////////////////////////////////
// Menu Handlers

LRESULT CyoHashDlg2::OnMenuPause( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    IntVector items = GetSelectedItems();
    for (IntVector::const_iterator i = items.begin(); i != items.end(); ++i)
    {
        int key = GetItemKey( *i );
        HashData hashData = SafeGetHashData( key );

        ::ResetEvent( hashData.resumeEvent );
        ::SetEvent( hashData.pauseEvent );
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuResume( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    IntVector items = GetSelectedItems();
    for (IntVector::const_iterator i = items.begin(); i != items.end(); ++i)
    {
        int key = GetItemKey( *i );
        HashData hashData = SafeGetHashData( key );

        ::ResetEvent( hashData.pauseEvent );
        ::SetEvent( hashData.resumeEvent );
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    IntVector items = GetSelectedItems();
    for (IntVector::const_iterator i = items.begin(); i != items.end(); ++i)
    {
        int key = GetItemKey( *i );
        HashData hashData = SafeGetHashData( key );

        ::SetEvent( hashData.cancelEvent );
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuCopyHash( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    IntVector items = GetSelectedItems();
    if (items.size() != 1)
    {
        bHandled = TRUE;
        return 0;
    }

    int key = GetItemKey( items[ 0 ] );
    HashData hashData = SafeGetHashData( key );

    if (::OpenClipboard( NULL ) && ::EmptyClipboard())
    {
        CStrBufW hash( hashData.hash );
        size_t numChars = (wcslen( hash ) + 1);
        HGLOBAL hglb = ::GlobalAlloc( GMEM_MOVEABLE, numChars * sizeof( wchar_t ));
        if (hglb != NULL)
        {
            wchar_t* dest = (wchar_t*)::GlobalLock( hglb );
            wcscpy_s( dest, numChars, hash );
            ::GlobalUnlock( hglb );
            ::SetClipboardData( CF_UNICODETEXT, hglb ); 
        }
        ::CloseClipboard();
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuRecalculate(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
    IntVector items = GetSelectedItems();
    for (IntVector::const_reverse_iterator i = items.rbegin(); i != items.rend(); ++i)
    {
        int key = GetItemKey(*i);
        HashData hashData = SafeGetHashData(key);
        if (hashData.completed || hashData.cancelled)
        {
            BeginHashing(hashData.pathname, hashData.algorithm);
            m_hashData.RemoveKey(key);
            ListView_DeleteItem(m_listWnd, *i);
        }
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuClear(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
    ClearSelectedItems();

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuClearAll( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    SafeClearAll();

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuHashFile( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    CStringW pathname = L"";
    CStringW algorithm = L"";

    BrowseDlg dlg( pathname, algorithm );
    if (dlg.DoModal() == IDOK)
        BeginHashing( pathname, algorithm );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuExportHashes( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    CStringW pathname;
    if (SaveFileDialog(pathname))
    {
        LPCWSTR extension = ::PathFindExtensionW(pathname);
        ExportFormat exportFormat = ExportFormat::Txt;
        if (_wcsicmp(extension, L".json") == 0)
            exportFormat = ExportFormat::Json;
        else if (_wcsicmp(extension, L".csv") == 0)
            exportFormat = ExportFormat::Csv;

        CAtlList<CStringW> list;
        GetHashesForExport(list, exportFormat);

        std::wofstream file(pathname);
        switch (exportFormat)
        {
        case ExportFormat::Json: ExportHashesJson(file, list); break;
        case ExportFormat::Csv: ExportHashesCsv(file, list); break;
        default: ExportHashesTxt(file, list);
        }

        std::wostringstream msg;
        msg << L"Exported to:\n\n" << (LPCWSTR)pathname;
        ::MessageBoxW(m_hWnd, msg.str().c_str(), L"CyoHash - Export", MB_OK | MB_ICONINFORMATION);
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuExportForensicPdf( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    IntVector items = GetSelectedItems();
    std::vector<ForensicHashReportData> reportItems;

    {
        CComCritSecLock<CComCriticalSection> lock( m_sync );
        for (IntVector::const_iterator i = items.begin(); i != items.end(); ++i)
        {
            int key = GetItemKey( *i );
            HashData hashData = SafeGetHashData( key );
            if (!hashData.completed)
                continue;

            ForensicHashReportData data;
            data.pathname = hashData.pathname;
            data.algorithm = hashData.algorithmLong;
            data.hash = hashData.hash;
            data.fileSize = hashData.fileSize;
            data.hashStartedUtc = hashData.hashStartedUtc;
            data.hashedUtc = hashData.hashedUtc;
            data.suppressUserProfile = false;
            reportItems.push_back( data );
        }
    }

    if (reportItems.empty())
    {
        ::MessageBoxW( m_hWnd, L"Select one or more completed hash results before exporting a forensic PDF.",
            L"CyoHash - Forensic Report", MB_OK | MB_ICONINFORMATION );
        bHandled = TRUE;
        return 0;
    }

    ForensicReportDlg details;
    if (details.DoModal( m_hWnd ) != IDOK)
    {
        bHandled = TRUE;
        return 0;
    }

    for (size_t i = 0; i < reportItems.size(); ++i)
    {
        reportItems[i].caseName = details.caseName;
        reportItems[i].examiner = details.examiner;
        reportItems[i].evidenceItem = details.evidenceItem;
        reportItems[i].suppressUserProfile = details.suppressUserProfile;
    }

    // Sort only the PDF export copy. The order displayed in the main results
    // window remains unchanged. Up to five sort fields can participate,
    // with each level independently ascending or descending.
    std::stable_sort( reportItems.begin(), reportItems.end(),
        [this, &details]( const ForensicHashReportData& left, const ForensicHashReportData& right ) -> bool
        {
            for (int level = 0; level < 5; ++level)
            {
                const ForensicReportDlg::PdfSortLevel& sort = details.sortLevels[level];
                if (sort.field == ForensicReportDlg::PdfSortField::None)
                    continue;

                int comparison = 0;
                switch (sort.field)
                {
                case ForensicReportDlg::PdfSortField::FileName:
                    comparison = CStringW( ::PathFindFileNameW( (LPCWSTR)left.pathname ) ).CompareNoCase(
                        CStringW( ::PathFindFileNameW( (LPCWSTR)right.pathname ) ) );
                    break;

                case ForensicReportDlg::PdfSortField::FileSize:
                    comparison = (left.fileSize < right.fileSize) ? -1 :
                        ((left.fileSize > right.fileSize) ? 1 : 0);
                    break;

                case ForensicReportDlg::PdfSortField::Algorithm:
                    comparison = left.algorithm.CompareNoCase( right.algorithm );
                    break;

                case ForensicReportDlg::PdfSortField::PathName:
                    comparison = this->GetDirectoryPath( left.pathname ).CompareNoCase( this->GetDirectoryPath( right.pathname ) );
                    break;

                case ForensicReportDlg::PdfSortField::HashValue:
                    comparison = left.hash.CompareNoCase( right.hash );
                    break;

                default:
                    break;
                }

                if (comparison != 0)
                    return sort.ascending ? (comparison < 0) : (comparison > 0);
            }

            // stable_sort preserves the pre-export order for complete ties.
            return false;
        } );

    CComPtr<IFileDialog> fileDialog;
    if (FAILED(fileDialog.CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER)))
    {
        ::MessageBoxW( m_hWnd, L"Unable to open the Save PDF dialog.", L"CyoHash - Forensic Report", MB_OK | MB_ICONERROR );
        bHandled = TRUE;
        return 0;
    }

    COMDLG_FILTERSPEC filters[] = { { L"PDF files", L"*.pdf" }, { L"All files", L"*.*" } };
    fileDialog->SetFileTypes( ARRAYSIZE(filters), filters );
    fileDialog->SetFileTypeIndex( 1 );
    fileDialog->SetDefaultExtension( L"pdf" );

    CStringW suggested;
    if (reportItems.size() == 1)
    {
        CStringW filename = ::PathFindFileNameW( (LPCWSTR)reportItems[0].pathname );
        int dot = filename.ReverseFind( L'.' );
        if (dot > 0) filename = filename.Left( dot );
        suggested = filename + L"_Hash_Report.pdf";
    }
    else
    {
        suggested = L"CyoHash_Forensic_Hash_Report.pdf";
    }
    fileDialog->SetFileName( suggested );

    if (SUCCEEDED(fileDialog->Show( m_hWnd )))
    {
        CComPtr<IShellItem> item;
        if (SUCCEEDED(fileDialog->GetResult( &item )))
        {
            PWSTR path = NULL;
            if (SUCCEEDED(item->GetDisplayName( SIGDN_FILESYSPATH, &path )))
            {
                CStringW error;
                if (pdfreport::SaveForensicHashReportBatch( path, reportItems, error ))
                {
                    CStringW message;
                    message.Format( L"Forensic hash report exported successfully.\n\n%u hash result(s) included.",
                        (unsigned int)reportItems.size() );
                    ::MessageBoxW( m_hWnd, message, L"CyoHash - Forensic Report", MB_OK | MB_ICONINFORMATION );
                }
                else
                {
                    ::MessageBoxW( m_hWnd, error, L"CyoHash - Forensic Report", MB_OK | MB_ICONERROR );
                }
                ::CoTaskMemFree( path );
            }
        }
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSelectMatchingHashes( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    IntVector items = GetSelectedItems();
    if (items.empty())
    {
        bHandled = TRUE;
        return 0;
    }

    int sourceKey = GetItemKey( items[0] );
    HashData source = SafeGetHashData( sourceKey );
    if (!source.completed)
    {
        bHandled = TRUE;
        return 0;
    }

    int count = ListView_GetItemCount( m_listWnd );
    for (int i = 0; i < count; ++i)
        ListView_SetItemState( m_listWnd, i, 0, LVIS_SELECTED | LVIS_FOCUSED );

    m_matchingHighlightKeys.clear();
    int firstMatch = -1;
    for (int i = 0; i < count; ++i)
    {
        int key = GetItemKey( i );
        HashData candidate = SafeGetHashData( key );
        if (!candidate.completed)
            continue;

        if (_wcsicmp( candidate.algorithm, source.algorithm ) == 0 &&
            _wcsicmp( candidate.hash, source.hash ) == 0)
        {
            ListView_SetItemState( m_listWnd, i, LVIS_SELECTED, LVIS_SELECTED );
            m_matchingHighlightKeys.push_back( key );
            if (firstMatch < 0)
                firstMatch = i;
        }
    }

    if (firstMatch >= 0)
    {
        ListView_SetItemState( m_listWnd, firstMatch, LVIS_FOCUSED, LVIS_FOCUSED );
        ListView_EnsureVisible( m_listWnd, firstMatch, FALSE );
    }
    ::InvalidateRect( m_listWnd, NULL, FALSE );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuResetColumns( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    ResetColumnWidths();
    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuAlwaysOnTop( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    m_alwaysOnTop = !m_alwaysOnTop;
    SetWindowPos(m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    if (m_hMainMenu != NULL)
    {
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_ALWAYS_ON_TOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_UNSORTED, MF_BYCOMMAND | (m_sortBy == SortBy::Unsorted ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_FILE, MF_BYCOMMAND | (m_sortBy == SortBy::SortByFile ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_ALGORITHM, MF_BYCOMMAND | (m_sortBy == SortBy::SortByAlgorithm ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_HASH, MF_BYCOMMAND | (m_sortBy == SortBy::SortByHash ? MF_CHECKED : MF_UNCHECKED) );
    }

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuAbout( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    AboutDlg dlg;
    dlg.DoModal();

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuMD5( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    HashDroppedFiles( L"MD5" );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSHA1( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    HashDroppedFiles( L"SHA1" );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSHA1Base32( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    HashDroppedFiles( L"SHA1-BASE32" );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSHA256( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    HashDroppedFiles( L"SHA256" );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSHA384( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    HashDroppedFiles( L"SHA384" );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSHA512( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    HashDroppedFiles( L"SHA512" );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuCRC32( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    HashDroppedFiles( L"CRC32" );

    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuUnsorted( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    m_sortBy = SortBy::Unsorted;
    SortList();
    if (m_hMainMenu != NULL)
    {
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_ALWAYS_ON_TOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_UNSORTED, MF_BYCOMMAND | (m_sortBy == SortBy::Unsorted ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_FILE, MF_BYCOMMAND | (m_sortBy == SortBy::SortByFile ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_ALGORITHM, MF_BYCOMMAND | (m_sortBy == SortBy::SortByAlgorithm ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_HASH, MF_BYCOMMAND | (m_sortBy == SortBy::SortByHash ? MF_CHECKED : MF_UNCHECKED) );
    }


    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSortByFile( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    m_sortBy = SortBy::SortByFile;
    SortList();
    if (m_hMainMenu != NULL)
    {
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_ALWAYS_ON_TOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_UNSORTED, MF_BYCOMMAND | (m_sortBy == SortBy::Unsorted ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_FILE, MF_BYCOMMAND | (m_sortBy == SortBy::SortByFile ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_ALGORITHM, MF_BYCOMMAND | (m_sortBy == SortBy::SortByAlgorithm ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_HASH, MF_BYCOMMAND | (m_sortBy == SortBy::SortByHash ? MF_CHECKED : MF_UNCHECKED) );
    }


    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSortByAlgorithm( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    m_sortBy = SortBy::SortByAlgorithm;
    SortList();
    if (m_hMainMenu != NULL)
    {
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_ALWAYS_ON_TOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_UNSORTED, MF_BYCOMMAND | (m_sortBy == SortBy::Unsorted ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_FILE, MF_BYCOMMAND | (m_sortBy == SortBy::SortByFile ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_ALGORITHM, MF_BYCOMMAND | (m_sortBy == SortBy::SortByAlgorithm ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_HASH, MF_BYCOMMAND | (m_sortBy == SortBy::SortByHash ? MF_CHECKED : MF_UNCHECKED) );
    }


    bHandled = TRUE;
    return 0;
}

LRESULT CyoHashDlg2::OnMenuSortByHash( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
    m_sortBy = SortBy::SortByHash;
    SortList();
    if (m_hMainMenu != NULL)
    {
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_ALWAYS_ON_TOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_UNSORTED, MF_BYCOMMAND | (m_sortBy == SortBy::Unsorted ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_FILE, MF_BYCOMMAND | (m_sortBy == SortBy::SortByFile ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_ALGORITHM, MF_BYCOMMAND | (m_sortBy == SortBy::SortByAlgorithm ? MF_CHECKED : MF_UNCHECKED) );
        ::CheckMenuItem( m_hMainMenu, IDC_MENU_SORTBY_HASH, MF_BYCOMMAND | (m_sortBy == SortBy::SortByHash ? MF_CHECKED : MF_UNCHECKED) );
    }


    bHandled = TRUE;
    return 0;
}

//////////////////////////////////////////////////////////////////////
// Implementation

void CyoHashDlg2::ReadLastSettings()
{
    HKEY hKey;
    if (::RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\CyoHash", 0, KEY_QUERY_VALUE, &hKey ) != ERROR_SUCCESS)
        return;

    m_lastX = ReadIntFromRegistry( hKey, L"x", -1 );
    m_lastY = ReadIntFromRegistry( hKey, L"y", -1 );
    m_lastWidth = ReadIntFromRegistry( hKey, L"cx", -1 );
    m_lastHeight  = ReadIntFromRegistry( hKey, L"cy", -1 );

    m_sortBy = (SortBy)ReadIntFromRegistry( hKey, L"sortBy", (int)SortBy::Unsorted );

    m_alwaysOnTop = ReadIntFromRegistry( hKey, L"alwaysOnTop", 0 ) != 0;

    m_columnWidths[0] = ReadIntFromRegistry( hKey, L"columnFileWidth", -1 );
    m_columnWidths[1] = ReadIntFromRegistry( hKey, L"columnAlgorithmWidth", -1 );
    m_columnWidths[2] = ReadIntFromRegistry( hKey, L"columnHashWidth", -1 );

    ::RegCloseKey( hKey );
}

int CyoHashDlg2::ReadIntFromRegistry( HKEY hKey, LPCWSTR name, int default_ )
{
    DWORD type, len, data;
    if (::RegQueryValueExW( hKey, name, NULL, &type, NULL, &len ) != ERROR_SUCCESS)
        return default_;
    if (type != REG_DWORD || len != sizeof( DWORD ))
        return default_;
    if (::RegQueryValueExW( hKey, name, NULL, &type, (BYTE*)&data, &len ) != ERROR_SUCCESS)
        return default_;
    return (int)data;
}

void CyoHashDlg2::SaveCurrentSettings()
{
    HKEY hKey;
    if (::RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\CyoHash", NULL, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL ) != ERROR_SUCCESS)
        return;

    RECT rect = { 0 };
    GetWindowRect( &rect );
    WriteIntToRegistry( hKey, L"x", rect.left );
    WriteIntToRegistry( hKey, L"y", rect.top );
    WriteIntToRegistry( hKey, L"cx", rect.right - rect.left );
    WriteIntToRegistry( hKey, L"cy", rect.bottom - rect.top );

    WriteIntToRegistry(hKey, L"sortBy", (int)m_sortBy);

    WriteIntToRegistry(hKey, L"alwaysOnTop", m_alwaysOnTop ? 1 : 0);

    for (int i = 0; i < 3; ++i)
        m_columnWidths[i] = ListView_GetColumnWidth( m_listWnd, i );
    WriteIntToRegistry( hKey, L"columnFileWidth", m_columnWidths[0] );
    WriteIntToRegistry( hKey, L"columnAlgorithmWidth", m_columnWidths[1] );
    WriteIntToRegistry( hKey, L"columnHashWidth", m_columnWidths[2] );

    ::RegCloseKey( hKey );
}

void CyoHashDlg2::WriteIntToRegistry( HKEY hKey, LPCWSTR name, int value )
{
    ::RegSetValueExW( hKey, name, 0, REG_DWORD, (BYTE*)&value, sizeof( value ));
}

void CyoHashDlg2::InitList()
{
    m_listWnd = GetDlgItem( IDC_LIST );

    LVCOLUMN col1 = { 0 };
    col1.mask = LVCF_TEXT;
    col1.pszText = L"File";
    ListView_InsertColumn( m_listWnd, 0, &col1 );

    LVCOLUMN col2 = { 0 };
    col2.mask = LVCF_TEXT;
    col2.pszText = L"Algorithm";
    ListView_InsertColumn( m_listWnd, 1, &col2 );

    LVCOLUMN col3 = { 0 };
    col3.mask = LVCF_TEXT;
    col3.pszText = L"Hash";
    ListView_InsertColumn( m_listWnd, 2, &col3 );

    DWORD styles = (LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetExtendedListViewStyleEx( m_listWnd, styles, styles );

    if (m_columnWidths[0] > 20 && m_columnWidths[1] > 20 && m_columnWidths[2] > 20)
    {
        for (int i = 0; i < 3; ++i)
            ListView_SetColumnWidth( m_listWnd, i, m_columnWidths[i] );
    }
    else
    {
        ResetColumnWidths();
    }
}

void CyoHashDlg2::ResizeList()
{
    RECT rect = { 0 };
    ::GetClientRect( m_hWnd, &rect );
    m_listWnd.MoveWindow( &rect );
}

void CyoHashDlg2::ResetColumnWidths()
{
    RECT rect = { 0 };
    ::GetClientRect( m_hWnd, &rect );
    int width = max( rect.right - rect.left, 240 );

    const int algorithmWidth = 90;
    const int minimumHashWidth = 260;
    int hashWidth = minimumHashWidth;
    int fileWidth = width - algorithmWidth - hashWidth - 8;

    if (fileWidth < 180)
    {
        fileWidth = max( 120, (width - algorithmWidth) / 2 );
        hashWidth = max( 120, width - algorithmWidth - fileWidth - 8 );
    }

    m_columnWidths[0] = fileWidth;
    m_columnWidths[1] = algorithmWidth;
    m_columnWidths[2] = hashWidth;

    for (int i = 0; i < 3; ++i)
        ListView_SetColumnWidth( m_listWnd, i, m_columnWidths[i] );
}

CStringW CyoHashDlg2::GetDirectoryPath( const CStringW& pathname ) const
{
    CStringW directory( pathname );
    wchar_t* buffer = directory.GetBuffer( MAX_PATH );
    if (!::PathRemoveFileSpecW( buffer ))
    {
        directory.ReleaseBuffer();
        return CStringW();
    }
    directory.ReleaseBuffer();
    return directory;
}

bool CyoHashDlg2::IsCrossPathDuplicate( int key )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );
    HashData source = SafeGetHashData( key );
    if (!source.completed || source.hash.IsEmpty())
        return false;

    CStringW sourceDirectory = GetDirectoryPath( source.pathname );
    if (sourceDirectory.IsEmpty())
        return false;

    for (POSITION pos = m_hashData.GetStartPosition(); pos != NULL; )
    {
        HashMap::CPair* pair = m_hashData.GetNext( pos );
        if (pair->m_key == key)
            continue;

        const HashData& candidate = pair->m_value;
        if (!candidate.completed || candidate.hash.IsEmpty())
            continue;

        if (_wcsicmp( candidate.algorithm, source.algorithm ) != 0 ||
            _wcsicmp( candidate.hash, source.hash ) != 0)
            continue;

        CStringW candidateDirectory = GetDirectoryPath( candidate.pathname );
        if (!candidateDirectory.IsEmpty() && _wcsicmp( candidateDirectory, sourceDirectory ) != 0)
            return true;
    }

    return false;
}

bool CyoHashDlg2::IsMatchingHighlightKey( int key ) const
{
    for (IntVector::const_iterator i = m_matchingHighlightKeys.begin(); i != m_matchingHighlightKeys.end(); ++i)
    {
        if (*i == key)
            return true;
    }
    return false;
}

int CyoHashDlg2::GetItemKey( int item )
{
    LVITEM lvi = { 0 };
    lvi.mask = LVIF_PARAM;
    lvi.iItem = item;
    ListView_GetItem( m_listWnd, &lvi );
    return (int)lvi.lParam;
}

int CyoHashDlg2::FindItem( int key )
{
    LVFINDINFO lvfi = { 0 };
    lvfi.flags = LVFI_PARAM;
    lvfi.lParam = key;
    return ListView_FindItem( m_listWnd, -1, &lvfi );
}

void CyoHashDlg2::ClearSelectedItems()
{
    IntVector items = GetSelectedItems();
    for (IntVector::const_reverse_iterator i = items.rbegin(); i != items.rend(); ++i)
    {
        int key = GetItemKey( *i );
        HashData hashData = SafeGetHashData( key );
        if (hashData.completed || hashData.cancelled)
        {
            m_hashData.RemoveKey( key );
            ListView_DeleteItem( m_listWnd, *i );
        }
    }
}

CyoHashDlg2::IntVector CyoHashDlg2::GetSelectedItems()
{
    return GetItems(LVNI_SELECTED);
}

CyoHashDlg2::IntVector CyoHashDlg2::GetAllItems()
{
    return GetItems(LVNI_ALL);
}

CyoHashDlg2::IntVector CyoHashDlg2::GetItems(DWORD flags)
{
    IntVector items;

    int item = -1;
    while (true)
    {
        item = ListView_GetNextItem(m_listWnd, item, flags);
        if (item == -1)
            break;
        items.push_back(item);
    }

    return items;
}

void CyoHashDlg2::ShowPopupMenu( int x, int y )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    int submenu = SUBMENU_LIST;

    IntVector items = GetSelectedItems();
    if (!items.empty())
    {
        bool hashing = false;
        bool paused = false;
        bool cancelled = false;
        bool completed = false;
        for (IntVector::const_iterator i = items.begin(); i != items.end(); ++i)
        {
            int key = GetItemKey( *i );
            HashData hashData = SafeGetHashData( key );
            hashing |= (!hashData.paused && !hashData.cancelled && !hashData.completed);
            paused |= hashData.paused;
            cancelled |= hashData.cancelled;
            completed |= hashData.completed;
        }
        ATLASSERT( hashing || paused || cancelled || completed );

        if (hashing && paused)
            submenu = SUBMENU_PAUSERESUME;
        else if (hashing)
            submenu = SUBMENU_PAUSE;
        else if (paused)
            submenu = SUBMENU_RESUME;
        else if (cancelled)
            submenu = SUBMENU_CANCELLED;
        else
            submenu = SUBMENU_COMPLETED;
    }

    HMENU hSubMenu = ::GetSubMenu( m_hMenu, submenu );
    ATLASSERT( hSubMenu != NULL );

    ::CheckMenuItem( hSubMenu, IDC_MENU_UNSORTED, MF_BYCOMMAND | (m_sortBy == SortBy::Unsorted ? MF_CHECKED : MF_UNCHECKED) );
    ::CheckMenuItem( hSubMenu, IDC_MENU_SORTBY_FILE, MF_BYCOMMAND | (m_sortBy == SortBy::SortByFile ? MF_CHECKED : MF_UNCHECKED) );
    ::CheckMenuItem( hSubMenu, IDC_MENU_SORTBY_ALGORITHM, MF_BYCOMMAND | (m_sortBy == SortBy::SortByAlgorithm ? MF_CHECKED : MF_UNCHECKED) );
    ::CheckMenuItem( hSubMenu, IDC_MENU_SORTBY_HASH, MF_BYCOMMAND | (m_sortBy == SortBy::SortByHash ? MF_CHECKED : MF_UNCHECKED) );

    ::CheckMenuItem(hSubMenu, IDC_MENU_ALWAYS_ON_TOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED) );

    UINT matchingFlags = MF_BYCOMMAND | MF_GRAYED;
    if (submenu == SUBMENU_COMPLETED && items.size() == 1)
    {
        int selectedKey = GetItemKey( items[0] );
        HashData selected = SafeGetHashData( selectedKey );
        if (selected.completed)
        {
            int matches = 0;
            for (POSITION pos = m_hashData.GetStartPosition(); pos != NULL; )
            {
                HashMap::CPair* pair = m_hashData.GetNext( pos );
                const HashData& candidate = pair->m_value;
                if (candidate.completed &&
                    _wcsicmp( candidate.algorithm, selected.algorithm ) == 0 &&
                    _wcsicmp( candidate.hash, selected.hash ) == 0)
                {
                    ++matches;
                }
            }
            if (matches > 1)
                matchingFlags = MF_BYCOMMAND | MF_ENABLED;
        }
    }
    ::EnableMenuItem( hSubMenu, IDC_MENU_SELECT_MATCHING_HASHES, matchingFlags );

    ::TrackPopupMenu( hSubMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, x, y, 0, m_hWnd, NULL );
}

void CyoHashDlg2::HashDroppedFiles( LPCWSTR algorithm )
{
    for (POSITION pos = m_droppedFiles.GetHeadPosition(); pos != NULL; )
    {
        CStringW pathname = m_droppedFiles.GetNext( pos );
        BeginHashing( pathname, algorithm );
    }
}

DWORD WINAPI CyoHashDlg2::StaticPipeThread( LPVOID param )
{
    ((CyoHashDlg2*)param)->PipeThread();
    return 0;
}

void CyoHashDlg2::PipeThread()
{
    NamedPipeServer pipe;
    NamedPipeServerCallback callback( *this );
    pipe.Run( m_pipeName, &callback, m_exitEvent );
}

void CyoHashDlg2::OnPipeReady()
{
    ATLASSERT( m_pipeReadyEvent != NULL );
    ::SetEvent( m_pipeReadyEvent );
}

void CyoHashDlg2::OnMessageReceived( LPBYTE start, DWORD length )
{
    wchar_t* begin = (wchar_t*)start;
    wchar_t* end = (wchar_t*)(start + length);
    wchar_t* pos = begin;
    for (; pos < end; ++pos)
    {
        if (*pos == L'\n')
            break;
    }
    if (pos == end)
        return; //no newline found :(

    *pos = L'\x0';
    CStringW pathname = begin;
    CStringW algorithm = (pos + 1);

    ShowWindow( SW_RESTORE );
    SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    if (!m_alwaysOnTop)
        SetWindowPos(HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    BeginHashing( pathname, algorithm );
}

bool CyoHashDlg2::BeginHashing( LPCWSTR pathname, LPCWSTR algorithm )
{
    CStringW str = pathname;
    int start = 0;
    while (true)
    {
        CStringW curr = str.Tokenize( L"|", start );
        if (start < 0)
            break;

        if (::PathIsDirectoryW(curr))
        {
            wchar_t szCompactPath[49];
            ::PathCompactPathExW(szCompactPath, curr, _countof(szCompactPath), 0);
            std::wostringstream title;
            title << L"CyoHash - " << szCompactPath;

            bool recurse = false;
            const int MaxFilesWithoutConfirmation = 50;

            int numFiles = 0, numFolders = 0;
            CountFilesToHash(curr, false, numFiles, numFolders);

            if (numFiles == 0)
            {
                ::MessageBoxW(m_hWnd, L"There are no files in this folder!", title.str().c_str(), MB_OK);
                return false;
            }

            if (numFolders >= 1)
            {
                int res = ::MessageBoxW(m_hWnd, L"Do you want to hash all files in all subfolders?", title.str().c_str(), MB_YESNO | MB_DEFBUTTON2 | MB_ICONEXCLAMATION);
                if (res == IDYES)
                {
                    numFiles = numFolders = 0;
                    CountFilesToHash(curr, recurse = true, numFiles, numFolders);
                }
            }

            if (numFiles > MaxFilesWithoutConfirmation)
            {
                std::wostringstream msg;
                msg << numFiles << L" files have been found.\n\nDo you want to hash them all?";
                int res = ::MessageBoxW(m_hWnd, msg.str().c_str(), title.str().c_str(), MB_YESNO | MB_DEFBUTTON2 | MB_ICONEXCLAMATION);
                if (res != IDYES)
                    return false;
            }

            FindFilesToHash(curr, recurse, algorithm);
        }
        else
        {
            BeginHashingFile(curr, algorithm);
        }
    }

    return true;
}

void CyoHashDlg2::CountFilesToHash(const CStringW& path, bool recurse, int& numFiles, int& numFolders)
{
    WIN32_FIND_DATAW fd = { 0 };
    HANDLE hSearch = ::FindFirstFileW(path + L"\\*.*", &fd);
    if (hSearch != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((std::wcscmp(fd.cFileName, L".") != 0)
                && (std::wcscmp(fd.cFileName, L"..") != 0))
            {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    ++numFiles;
                else
                {
                    ++numFolders;
                    if (recurse)
                        CountFilesToHash(path + L'\\' + fd.cFileName, recurse, numFiles, numFolders);
                }
            }
        } while (::FindNextFileW(hSearch, &fd));
        ::FindClose(hSearch);
    }
}

void CyoHashDlg2::FindFilesToHash(const CStringW& path, bool recurse, LPCWSTR algorithm)
{
    WIN32_FIND_DATAW fd = { 0 };
    HANDLE hSearch = ::FindFirstFileW(path + L"\\*.*", &fd);
    if (hSearch != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((std::wcscmp(fd.cFileName, L".") != 0)
                && (std::wcscmp(fd.cFileName, L"..") != 0))
            {
                CString fullPathname = (path + L'\\' + fd.cFileName);
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    BeginHashingFile(fullPathname, algorithm);
                else if (recurse)
                    FindFilesToHash(fullPathname, recurse, algorithm);
            }
        } while (::FindNextFileW(hSearch, &fd));
        ::FindClose(hSearch);
    }
}

void CyoHashDlg2::BeginHashingFile(LPCWSTR pathname, LPCWSTR algorithm)
{
    int key = SafeCreateHashData(pathname, algorithm);

    HANDLE readyEvent = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    ThreadData data = { *this, key, readyEvent };

    DWORD threadId;
    HANDLE thread = ::CreateThread(NULL, 0, &StaticHashThread, &data, CREATE_SUSPENDED, &threadId);
    SafeSetThreadHandle(key, thread);
    ::ResumeThread(thread);
    if (::WaitForSingleObject(readyEvent, 30000) != WAIT_OBJECT_0)
    {
        //::OutputDebugStringW( L"ERROR: WaitForSingleObject failed\n" );
        throw;
    }
}

DWORD WINAPI CyoHashDlg2::StaticHashThread( LPVOID param )
{
    ThreadData* data = (ThreadData*)param;
    data->dlg.HashThread( data );
    return 0;
}

void CyoHashDlg2::HashThread( ThreadData* data )
{
    int key = data->key;
    ::SetEvent( data->readyEvent );

    SafeSetStarted( key );
    PostMessage( WM_HASHING_STARTED, (WPARAM)key );

    HashData hashData = SafeGetHashData( key );

    CStringW algorithm = hashData.algorithm;
    algorithm.MakeUpper();

    std::auto_ptr< IHasher > hasher;
    if (algorithm == _T("MD5"))
        hasher = std::auto_ptr< IHasher >( new MD5Hasher );
    else if (algorithm == _T("SHA1"))
        hasher = std::auto_ptr< IHasher >( new SHAHasher( sha1hash, true ));
    else if (algorithm == _T("SHA1-BASE32"))
        hasher = std::auto_ptr< IHasher >( new SHAHasher( sha1hash, false ));
    else if (algorithm == _T("SHA256"))
        hasher = std::auto_ptr< IHasher >( new SHAHasher( sha256hash, true ));
    else if (algorithm == _T("SHA384"))
        hasher = std::auto_ptr< IHasher >( new SHAHasher( sha384hash, true ));
    else if (algorithm == _T("SHA512"))
        hasher = std::auto_ptr< IHasher >( new SHAHasher( sha512hash, true ));
    else if (algorithm == _T("CRC32"))
        hasher = std::auto_ptr< IHasher >( new CRC32Hasher );
    else
        throw std::runtime_error( "Invalid algorithm" );

    try
    {
        hasher->Init();

        DWORD dwBufferSize = (1024 * 1024);
        std::vector<BYTE> buffer( dwBufferSize );
        BYTE* pBuffer = &buffer[ 0 ];

        // Hash file...
        ULARGE_INTEGER hashedFileSize = { 0 };
        {
            HANDLE hFile = ::CreateFileW( hashData.pathname, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL );
            utils::ensure< std::runtime_error >( hFile != INVALID_HANDLE_VALUE );
            utils::AutoCloseHandle closeFile( hFile );

            ULARGE_INTEGER totalsize = { 0 };
            totalsize.LowPart = ::GetFileSize( hFile, &totalsize.HighPart );
            hashedFileSize = totalsize;
            double dTotalSize = static_cast< double >( totalsize.QuadPart );

            ULARGE_INTEGER completed = { 0 };
            ULARGE_INTEGER remaining = totalsize;

            int lastPercentage = -1;
            DWORD lastTicks = 0;

            while (remaining.QuadPart != 0)
            {
                // Global cancel?
                if (::WaitForSingleObject( m_exitEvent, 0 ) == WAIT_OBJECT_0)
                    return;

                // Cancel?
                if (::WaitForSingleObject( hashData.cancelEvent, 0 ) == WAIT_OBJECT_0)
                {
                    SafeSetCancelled( key );
                    PostMessage( WM_HASHING_CANCELLED, (WPARAM)key );
                    return;
                }

                // Pause?
                if (::WaitForSingleObject( hashData.pauseEvent, 0 ) == WAIT_OBJECT_0)
                {
                    // Paused, wait for resume or terminate...
                    ::ResetEvent( hashData.pauseEvent );
                    SafeSetPaused( key, true );
                    PostMessage( WM_HASHING_PAUSED, (WPARAM)key, (LPARAM)lastPercentage );
                    HANDLE handles[ 3 ] = { hashData.resumeEvent, hashData.cancelEvent, m_exitEvent };
                    ::WaitForMultipleObjects( 3, handles, FALSE, INFINITE ); //no need to check return
                    if (::WaitForSingleObject( m_exitEvent, 0 ) == WAIT_OBJECT_0)
                        return;
                    SafeSetPaused( key, false );
                    if (::WaitForSingleObject( hashData.cancelEvent, 0 ) == WAIT_OBJECT_0)
                    {
                        SafeSetCancelled( key );
                        PostMessage( WM_HASHING_CANCELLED, (WPARAM)key );
                        return;
                    }
                    PostMessage( WM_HASHING_PROGRESS, (WPARAM)key, (LPARAM)lastPercentage );
                    lastTicks = ::GetTickCount();
                }

                // Determine block size...
                ULARGE_INTEGER blockSize;
                blockSize.QuadPart = __min( remaining.QuadPart, dwBufferSize );
                utils::ensure< std::runtime_error >( blockSize.HighPart == 0 );
                utils::ensure< std::runtime_error >( 1 <= blockSize.LowPart && blockSize.LowPart <= dwBufferSize );

                // Read block...
                DWORD dwBytesRead = 0;
                ::ReadFile( hFile, pBuffer, blockSize.LowPart, &dwBytesRead, NULL );

                // Hash block...
                hasher->HashBlock( pBuffer, blockSize.LowPart );

                // Calculations...
                completed.QuadPart += blockSize.QuadPart;
                remaining.QuadPart -= blockSize.QuadPart;

                // percentage = (complete / total) * 100
                double dCompleted = static_cast< double >( completed.QuadPart );
                double dPercentage = (dCompleted / dTotalSize) * 100.0;
                int percentage = (int)(dPercentage + 0.5);
                if (percentage != lastPercentage)
                {
                    DWORD ticks = ::GetTickCount();
                    if (ticks - lastTicks >= 500) //never send more than two messages per second!
                    {
                        PostMessage( WM_HASHING_PROGRESS, (WPARAM)key, (LPARAM)percentage );
                        lastPercentage = percentage;
                        lastTicks = ticks;
                    }
                }
            }
        }

        // Results...

        hasher->Stop();

        SafeSetCompleted( key, hasher.get(), hashedFileSize.QuadPart );

        PostMessage( WM_HASHING_COMPLETED, (WPARAM)key );
    }
    catch (const std::exception& /*err*/)
    {
        /*if (err.what() != NULL && *err.what() != '\x0')
            ::MessageBoxA( pDlg->m_hWnd, err.what(), "CyoHash", MB_OK | MB_ICONERROR );
        else
            ::MessageBoxA( pDlg->m_hWnd, "Unspecified error", "CyoHash", MB_OK | MB_ICONERROR );*/
    }
}

bool CyoHashDlg2::SaveFileDialog(CStringW& pathname) const
{
    CComPtr<IFileDialog> fileDialog;
    if (FAILED(fileDialog.CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER)))
        return false;

    COMDLG_FILTERSPEC fileTypes[] =
    {
        { L"JSON files", L"*.json" },
        { L"CSV files", L"*.csv" },
        { L"Text files", L"*.txt" },
        { L"All files", L"*.*" }
    };
    if (FAILED(fileDialog->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes)))
        return false;

    if (FAILED(fileDialog->SetFileTypeIndex(0)))
        return false;

    if (FAILED(fileDialog->SetDefaultExtension(L"json")))
        return false;

    if (FAILED(fileDialog->Show(NULL)))
        return false;

    HRESULT hres;
    CComPtr<IShellItem> shellItem;
    hres = fileDialog->GetResult(&shellItem);
    if (FAILED(hres))
    {
        wchar_t msg[1024];
        ::swprintf_s(msg, L"GetResult failed, hres=%0x", hres);
        ::MessageBoxW(m_hWnd, msg, L"CyoHash - Export", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    PWSTR pszFilePath = NULL;
    hres = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
    if (FAILED(hres))
    {
        wchar_t msg[1024];
        ::swprintf_s(msg, L"GetDisplayName failed, hres=%0x", hres);
        ::MessageBoxW(m_hWnd, msg, L"CyoHash - Export", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    pathname = pszFilePath;
    ::CoTaskMemFree(pszFilePath);
    return true;
}

void CyoHashDlg2::GetHashesForExport(CAtlList<CStringW>& list, ExportFormat exportFormat)
{
    CComCritSecLock<CComCriticalSection> lock(m_sync);

    IntVector items = GetAllItems();
    for (IntVector::const_iterator i = items.begin(); i != items.end(); ++i)
    {
        int key = GetItemKey(*i);
        HashData hashData = SafeGetHashData(key);
        if (!hashData.completed)
            continue;

        std::wostringstream msg;
        switch (exportFormat)
        {
        case ExportFormat::Json:
        {
            CStringW pathname = hashData.pathname;
            pathname.Replace(L"\\", L"\\\\");
            const wchar_t* const indent = L"    ";
            msg << indent << L"{\n";
            msg << indent << indent << L"\"file\": \"" << (LPCWSTR)pathname << L"\",\n";
            msg << indent << indent << L"\"algorithm\": \"" << (LPCWSTR)hashData.algorithm << L"\",\n";
            msg << indent << indent << L"\"hash\": \"" << (LPCWSTR)hashData.hash << L"\"\n";
            msg << indent << L"}";
            break;
        }
        case ExportFormat::Csv:
            msg << L"\"" << (LPCWSTR)hashData.pathname << L"\""
                << L"," << (LPCWSTR)hashData.algorithm
                << L"," << (LPCWSTR)hashData.hash;
            break;

        default: //TXT
            msg << (LPCWSTR)hashData.hash
                << L"  " << (LPCWSTR)hashData.pathname
                << L"  " << (LPCWSTR)hashData.algorithm;
            break;
        }
        list.AddTail(msg.str().c_str());
    }
}

void CyoHashDlg2::ExportHashesJson(std::wofstream& file, const CAtlList<CStringW>& list) const
{
    file << L"[\n";

    bool first = true;
    for (POSITION pos = list.GetHeadPosition(); pos != NULL; )
    {
        if (first)
            first = false;
        else
            file << L",\n";

        file << (LPCWSTR)list.GetNext(pos);
    }

    file << L"\n]\n";
}

void CyoHashDlg2::ExportHashesCsv(std::wofstream& file, const CAtlList<CStringW>& list) const
{
    file << L"File,Algorithm,Hash\n";
    for (POSITION pos = list.GetHeadPosition(); pos != NULL; )
        file << (LPCWSTR)list.GetNext(pos) << "\n";
}

void CyoHashDlg2::ExportHashesTxt(std::wofstream& file, const CAtlList<CStringW>& list) const
{
    for (POSITION pos = list.GetHeadPosition(); pos != NULL; )
        file << (LPCWSTR)list.GetNext(pos) << "\n";
}

//////////////////////////////////////////////////////////////////////
// Thread-safe functions

int CyoHashDlg2::SafeCreateHashData( LPCWSTR pathname, LPCWSTR algorithm )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    HashData& hashData = m_hashData[ m_nextKey ];
    hashData.pathname = pathname;
    hashData.algorithm = algorithm;
    hashData.fileSize = 0;
    ::ZeroMemory( &hashData.hashStartedUtc, sizeof(hashData.hashStartedUtc) );
    ::ZeroMemory( &hashData.hashedUtc, sizeof(hashData.hashedUtc) );
    hashData.paused = false;
    hashData.completed = false;
    hashData.cancelled = false;
    hashData.pauseEvent = ::CreateEventW( NULL, TRUE, FALSE, NULL );
    hashData.resumeEvent = ::CreateEventW( NULL, TRUE, FALSE, NULL );
    hashData.cancelEvent = ::CreateEventW( NULL, TRUE, FALSE, NULL );

    return m_nextKey++;
}

void CyoHashDlg2::SafeSetThreadHandle( int key, HANDLE thread )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    m_hashData[ key ].thread = thread;
}

CyoHashDlg2::HashData CyoHashDlg2::SafeGetHashData( int key )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    return m_hashData[ key ];
}

void CyoHashDlg2::SafeSetPaused( int key, bool paused )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    m_hashData[ key ].paused = paused;
}

void CyoHashDlg2::SafeSetStarted( int key )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    ::GetSystemTimeAsFileTime( &m_hashData[ key ].hashStartedUtc );
}

void CyoHashDlg2::SafeSetCompleted( int key, IHasher* hasher, ULONGLONG fileSize )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    CA2W hash( hasher->GetHash() );
    CT2W name( hasher->GetName() );
    HashData& hashData = m_hashData[ key ];
    hashData.hash = hash;
    hashData.algorithmLong = name;
    hashData.splitHash = (hasher->GetAlgorithm() == sha384hash || hasher->GetAlgorithm() == sha512hash);
    hashData.fileSize = fileSize;
    ::GetSystemTimeAsFileTime( &hashData.hashedUtc );
    hashData.paused = false;
    hashData.completed = true;
    hashData.cancelled = false;
}

void CyoHashDlg2::SafeSetCancelled( int key )
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    m_hashData[ key ].cancelled = true;
}

void CyoHashDlg2::SafeClearAll()
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    for (POSITION pos = m_hashData.GetStartPosition(); pos != NULL; )
    {
        POSITION curr = pos;
        HashMap::CPair* pair = m_hashData.GetNext( pos );
        int key = pair->m_key;
        HashData& hashData = pair->m_value;
        if (hashData.completed || hashData.cancelled)
        {
            m_hashData.RemoveAtPos( curr );
            int item = FindItem( key );
            ListView_DeleteItem( m_listWnd, item );
        }
    }
}

void CyoHashDlg2::SafeJoinAll()
{
    CComCritSecLock<CComCriticalSection> lock( m_sync );

    for (POSITION pos = m_hashData.GetStartPosition(); pos != NULL; )
    {
        HashMap::CPair* pair = m_hashData.GetNext( pos );
        if (::WaitForSingleObject( pair->m_value.thread, 30000 ) != WAIT_OBJECT_0)
        {
            //::OutputDebugStringW( L"ERROR: WaitForSingleObject failed\n" );
            throw;
        }
    }
}
