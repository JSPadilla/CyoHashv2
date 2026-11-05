#pragma once

// Measure with the control's actual dialog font (including Windows font scaling).
namespace dialoglayout
{
    inline RECT WorkArea(HWND window)
    {
        MONITORINFO info = { sizeof(info) };
        ::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
        return info.rcWork;
    }

    inline int TextWidth(HWND control, LPCWSTR text)
    {
        HDC dc = ::GetDC(control);
        HFONT font = reinterpret_cast<HFONT>(::SendMessageW(control, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? ::SelectObject(dc, font) : NULL;
        RECT bounds = { 0 };
        ::DrawTextW(dc, text, -1, &bounds, DT_CALCRECT | DT_NOPREFIX);
        if (oldFont) ::SelectObject(dc, oldFont);
        ::ReleaseDC(control, dc);
        return bounds.right;
    }

    inline RECT ChildRect(HWND dialog, HWND child)
    {
        RECT rect;
        ::GetWindowRect(child, &rect);
        ::MapWindowPoints(NULL, dialog, reinterpret_cast<POINT*>(&rect), 2);
        return rect;
    }

    inline void FitWindow(HWND window, int clientWidth, int clientHeight)
    {
        RECT outer, client;
        ::GetWindowRect(window, &outer);
        ::GetClientRect(window, &client);
        RECT work = WorkArea(window);
        int width = min(clientWidth + (outer.right - outer.left) - client.right, work.right - work.left);
        int height = min(clientHeight + (outer.bottom - outer.top) - client.bottom, work.bottom - work.top);
        int x = max(work.left, min(outer.left, work.right - width));
        int y = max(work.top, min(outer.top, work.bottom - height));
        ::SetWindowPos(window, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Grow text fields and the dialog together. Other controls that follow a
    // field on the same row, and right-aligned buttons, move by the same amount.
    inline void FitFields(HWND dialog, const int* ids, int count)
    {
        RECT client;
        ::GetClientRect(dialog, &client);
        int growth = 0;
        std::vector<RECT> fields;
        for (int i = 0; i < count; ++i)
        {
            HWND field = ::GetDlgItem(dialog, ids[i]);
            RECT rect = ChildRect(dialog, field);
            fields.push_back(rect);
            CStringW text;
            CWindow(field).GetWindowText(text);
            growth = max(growth, TextWidth(field, text) + 24 - (rect.right - rect.left));
        }
        RECT outer;
        ::GetWindowRect(dialog, &outer);
        RECT work = WorkArea(dialog);
        growth = min(growth, max(0, (work.right - work.left) - (outer.right - outer.left)));
        if (growth <= 0)
        {
            FitWindow(dialog, client.right, client.bottom);
            return;
        }

        for (HWND child = ::GetWindow(dialog, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
        {
            RECT rect = ChildRect(dialog, child);
            int id = ::GetDlgCtrlID(child);
            bool stretch = false, move = false;
            for (int i = 0; i < count; ++i)
            {
                if (id == ids[i]) stretch = true;
                if (rect.top < fields[i].bottom && rect.bottom > fields[i].top && rect.left >= fields[i].right)
                    move = true;
            }
            wchar_t type[32] = { 0 };
            ::GetClassNameW(child, type, _countof(type));
            bool button = (_wcsicmp(type, L"Button") == 0);
            bool group = button && ((::GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX);
            if (group || (!button && rect.right > client.right * 3 / 4 && rect.left < client.right / 2)) stretch = true;
            if (button && !group && rect.left > client.right / 2) move = true;
            ::MoveWindow(child, rect.left + (move && !stretch ? growth : 0), rect.top,
                rect.right - rect.left + (stretch ? growth : 0), rect.bottom - rect.top, TRUE);
        }
        FitWindow(dialog, client.right + growth, client.bottom);
        // Edits retain their old horizontal scroll offset after MoveWindow.
        // Reset it so newly available space actually reveals the full value.
        for (int i = 0; i < count; ++i)
        {
            HWND field = ::GetDlgItem(dialog, ids[i]);
            DWORD start = 0, end = 0;
            ::SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            ::SendMessageW(field, EM_SETSEL, 0, 0);
            ::SendMessageW(field, EM_SCROLLCARET, 0, 0);
            ::SendMessageW(field, EM_SETSEL, start, end);
        }
    }
}
