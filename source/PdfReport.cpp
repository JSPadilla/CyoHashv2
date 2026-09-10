#include "stdafx.h"
#include "PdfReport.h"
#include <iomanip>
#include <map>
#include <cmath>

namespace
{
    const int PAGE_WIDTH = 792;   // US Letter landscape: 11 x 8.5 inches
    const int PAGE_HEIGHT = 612;
    const int LEFT = 36;
    const int RIGHT = 756;
    const int FOOTER_Y = 20;
    const int TABLE_BOTTOM = 42;
    const int COL_FILE = 36;
    const int COL_SIZE = 200;
    const int COL_ALGORITHM = 310;
    const int COL_PATH = 395;
    const int COL_HASH = 445;
    const int COL_END = 756;
    const int TABLE_HEADER_HEIGHT = 24;
    const int LINE_HEIGHT = 11;

    std::string Narrow( const CStringW& value )
    {
        if (value.IsEmpty()) return std::string();
        int len = ::WideCharToMultiByte( CP_ACP, 0, value, value.GetLength(), NULL, 0, NULL, NULL );
        std::string result( len, '\0' );
        if (len > 0)
            ::WideCharToMultiByte( CP_ACP, 0, value, value.GetLength(), &result[0], len, NULL, NULL );
        return result;
    }

    std::string EscapePdf( const std::string& value )
    {
        std::string out;
        for (size_t i = 0; i < value.size(); ++i)
        {
            unsigned char c = (unsigned char)value[i];
            if (c == '\\' || c == '(' || c == ')') out += '\\';
            if (c == '\r' || c == '\n') out += ' ';
            else if (c >= 32) out += (char)c;
        }
        return out;
    }

    CStringW FormatUtc( const FILETIME& ft )
    {
        SYSTEMTIME st = {0};
        ::FileTimeToSystemTime( &ft, &st );
        CStringW s;
        s.Format( L"%04u-%02u-%02u %02u:%02u:%02u UTC", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
        return s;
    }

    CStringW FormatLocal( const FILETIME& ft )
    {
        SYSTEMTIME utc = {0}, local = {0};
        ::FileTimeToSystemTime( &ft, &utc );
        ::SystemTimeToTzSpecificLocalTime( NULL, &utc, &local );

        FILETIME localFt = {0};
        ::SystemTimeToFileTime( &local, &localFt );
        ULARGE_INTEGER uUtc, uLocal;
        uUtc.LowPart = ft.dwLowDateTime; uUtc.HighPart = ft.dwHighDateTime;
        uLocal.LowPart = localFt.dwLowDateTime; uLocal.HighPart = localFt.dwHighDateTime;
        LONGLONG minutes = ((LONGLONG)uLocal.QuadPart - (LONGLONG)uUtc.QuadPart) / (10LL * 1000LL * 1000LL * 60LL);
        wchar_t sign = minutes < 0 ? L'-' : L'+';
        if (minutes < 0) minutes = -minutes;
        CStringW s;
        s.Format( L"%04u-%02u-%02u %02u:%02u:%02u (UTC%c%02lld:%02lld)",
            local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
            sign, minutes / 60, minutes % 60 );
        return s;
    }

    CStringW FileNameOnly( const CStringW& path )
    {
        return CStringW(::PathFindFileNameW((LPCWSTR)path));
    }

    CStringW DirectoryOnly( const CStringW& path )
    {
        CStringW directory(path);
        wchar_t* buffer = directory.GetBuffer(directory.GetLength() + 1);
        if (!::PathRemoveFileSpecW(buffer))
        {
            directory.ReleaseBuffer();
            return CStringW();
        }
        directory.ReleaseBuffer();
        return directory;
    }

    CStringW SuppressUserProfileName( const CStringW& directory )
    {
        // This affects report presentation only. The original pathname retained by
        // CyoHash and used for hashing is never modified.
        CStringW result(directory);
        CStringW lower(directory);
        lower.MakeLower();

        const CStringW marker(L"\\users\\");
        const int markerPos = lower.Find(marker);
        if (markerPos < 0)
            return result;

        const int profileStart = markerPos + marker.GetLength();
        if (profileStart >= directory.GetLength())
            return result;

        int profileEnd = directory.Find(L'\\', profileStart);
        if (profileEnd < 0)
            profileEnd = directory.GetLength();

        if (profileEnd <= profileStart)
            return result;

        CStringW remainder;
        if (profileEnd < directory.GetLength())
            remainder = directory.Mid(profileEnd);

        return CStringW(L"%USERPROFILE%") + remainder;
    }

    ULONGLONG FileTimeValue( const FILETIME& ft )
    {
        ULARGE_INTEGER value;
        value.LowPart = ft.dwLowDateTime;
        value.HighPart = ft.dwHighDateTime;
        return value.QuadPart;
    }

    std::vector<std::string> Wrap( const std::string& text, size_t width )
    {
        std::vector<std::string> lines;
        if (text.empty()) { lines.push_back(""); return lines; }
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t count = __min(width, text.size() - pos);
            size_t end = pos + count;
            if (end < text.size())
            {
                size_t space = text.rfind(' ', end);
                if (space != std::string::npos && space > pos + width / 2) end = space;
            }
            lines.push_back(text.substr(pos, end - pos));
            pos = end;
            while (pos < text.size() && text[pos] == ' ') ++pos;
        }
        return lines;
    }

    void TextLine( std::ostringstream& c, const char* font, int size, int x, int y, const std::string& text )
    {
        c << "BT /" << font << " " << size << " Tf " << x << " " << y << " Td (" << EscapePdf(text) << ") Tj ET\n";
    }

    void DrawLine( std::ostringstream& c, int x1, int y1, int x2, int y2 )
    {
        // Explicitly reset the stroke color to black before every rule. This
        // prevents row background fills from affecting grid rendering in PDF
        // viewers that are sensitive to graphics-state ordering.
        c << "0 G 0.55 w " << x1 << " " << y1 << " m " << x2 << " " << y2 << " l S\n";
    }

    void DrawTableHeader( std::ostringstream& c, int top )
    {
        const int bottom = top - TABLE_HEADER_HEIGHT;
        c << "0.94 g " << LEFT << " " << bottom << " " << (RIGHT - LEFT) << " " << TABLE_HEADER_HEIGHT << " re f 0 g\n";
        DrawLine(c, LEFT, top, RIGHT, top);
        DrawLine(c, LEFT, bottom, RIGHT, bottom);
        DrawLine(c, COL_FILE, top, COL_FILE, bottom);
        DrawLine(c, COL_SIZE, top, COL_SIZE, bottom);
        DrawLine(c, COL_ALGORITHM, top, COL_ALGORITHM, bottom);
        DrawLine(c, COL_PATH, top, COL_PATH, bottom);
        DrawLine(c, COL_HASH, top, COL_HASH, bottom);
        DrawLine(c, COL_END, top, COL_END, bottom);
        TextLine(c, "F2", 9, COL_FILE + 6, bottom + 8, "File Name");
        TextLine(c, "F2", 9, COL_SIZE + 6, bottom + 8, "File Size (bytes)");
        TextLine(c, "F2", 9, COL_ALGORITHM + 6, bottom + 8, "Algorithm");
        TextLine(c, "F2", 9, COL_PATH + 6, bottom + 8, "Path");
        TextLine(c, "F2", 9, COL_HASH + 6, bottom + 8, "Hash Value");
    }

    int RowHeight( const ForensicHashReportData& item )
    {
        const std::vector<std::string> fileLines = Wrap(Narrow(FileNameOnly(item.pathname)), 29);
        const std::vector<std::string> algorithmLines = Wrap(Narrow(item.algorithm), 12);
        const std::vector<std::string> hashLines = Wrap(Narrow(item.hash), 70);
        size_t lineCount = __max(fileLines.size(), algorithmLines.size());
        lineCount = __max(lineCount, hashLines.size());
        int height = 8 + (int)lineCount * LINE_HEIGHT;
        if (height < 25) height = 25;
        return height;
    }

    std::wstring HashMatchKey( const ForensicHashReportData& item )
    {
        CStringW algorithm(item.algorithm);
        CStringW hash(item.hash);
        algorithm.MakeUpper();
        hash.MakeUpper();

        std::wstring key((LPCWSTR)algorithm);
        key += L"\x1f";
        key += (LPCWSTR)hash;
        return key;
    }

    std::vector<int> BuildMatchGroups( const std::vector<ForensicHashReportData>& data )
    {
        std::map<std::wstring, size_t> counts;
        for (size_t i = 0; i < data.size(); ++i)
            ++counts[HashMatchKey(data[i])];

        std::map<std::wstring, int> groupIds;
        std::vector<int> groups(data.size(), -1);
        int nextGroup = 0;

        for (size_t i = 0; i < data.size(); ++i)
        {
            const std::wstring key = HashMatchKey(data[i]);
            if (counts[key] < 2)
                continue;

            std::map<std::wstring, int>::iterator existing = groupIds.find(key);
            if (existing == groupIds.end())
            {
                groupIds[key] = nextGroup;
                groups[i] = nextGroup;
                ++nextGroup;
            }
            else
            {
                groups[i] = existing->second;
            }
        }

        return groups;
    }

    std::string PdfColorComponent( unsigned int value )
    {
        // Convert an 8-bit component to a PDF 0..1 decimal without relying on
        // the process locale. Values used here are 192..255, so three decimal
        // places remain unique for every component value.
        if (value >= 255)
            return "1.000";

        const unsigned int thousandths = (value * 1000u + 127u) / 255u;
        std::ostringstream valueText;
        valueText << "0." << std::setw(3) << std::setfill('0') << thousandths;
        return valueText.str();
    }

    void FillMatchHighlight( std::ostringstream& c, int top, int bottom, int group )
    {
        if (group < 0)
            return;

        // Generate a unique pastel RGB value for every duplicate group rather
        // than cycling through a finite palette. Multiplication by an odd
        // constant is a permutation in the 18-bit space, so group colors do
        // not repeat for the first 262,144 distinct match groups. Each 6-bit
        // channel is shifted into the 192..255 range to keep the fill light
        // enough for black table text and rules.
        const unsigned int packed =
            (static_cast<unsigned int>(group + 1) * 2654435761u) & 0x3ffffu;
        const unsigned int red = 192u + (packed & 0x3fu);
        const unsigned int green = 192u + ((packed >> 6) & 0x3fu);
        const unsigned int blue = 192u + ((packed >> 12) & 0x3fu);

        c << PdfColorComponent(red) << " "
          << PdfColorComponent(green) << " "
          << PdfColorComponent(blue) << " rg "
          << LEFT << " " << bottom << " " << (RIGHT - LEFT) << " " << (top - bottom)
          << " re f 0 g\n";
    }

    void DrawRow( std::ostringstream& c, int top, int height, const ForensicHashReportData& item, int matchGroup, int pathIndicator )
    {
        const int bottom = top - height;
        FillMatchHighlight(c, top, bottom, matchGroup);

        // Draw the complete grid after the background fill. Drawing both the
        // top and bottom rules for every row prevents highlighted rows from
        // covering a neighboring horizontal rule in Edge and other viewers.
        DrawLine(c, LEFT, top, RIGHT, top);
        DrawLine(c, LEFT, bottom, RIGHT, bottom);
        DrawLine(c, COL_FILE, top, COL_FILE, bottom);
        DrawLine(c, COL_SIZE, top, COL_SIZE, bottom);
        DrawLine(c, COL_ALGORITHM, top, COL_ALGORITHM, bottom);
        DrawLine(c, COL_PATH, top, COL_PATH, bottom);
        DrawLine(c, COL_HASH, top, COL_HASH, bottom);
        DrawLine(c, COL_END, top, COL_END, bottom);

        const std::vector<std::string> fileLines = Wrap(Narrow(FileNameOnly(item.pathname)), 29);
        const std::vector<std::string> algorithmLines = Wrap(Narrow(item.algorithm), 12);
        const std::vector<std::string> hashLines = Wrap(Narrow(item.hash), 70);
        int textY = top - 15;
        for (size_t i = 0; i < fileLines.size(); ++i)
            TextLine(c, "F1", 8, COL_FILE + 6, textY - (int)i * LINE_HEIGHT, fileLines[i]);

        std::ostringstream sizeText;
        sizeText << item.fileSize;
        TextLine(c, "F1", 8, COL_SIZE + 6, textY, sizeText.str());

        for (size_t i = 0; i < algorithmLines.size(); ++i)
            TextLine(c, "F1", 8, COL_ALGORITHM + 6, textY - (int)i * LINE_HEIGHT, algorithmLines[i]);

        std::ostringstream pathText;
        pathText << pathIndicator;
        TextLine(c, "F1", 8, COL_PATH + 19, textY, pathText.str());

        for (size_t i = 0; i < hashLines.size(); ++i)
            TextLine(c, "F3", 7, COL_HASH + 6, textY - (int)i * LINE_HEIGHT, hashLines[i]);
    }

    bool IsZeroFileTime( const FILETIME& ft )
    {
        return ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0;
    }

    void HashingTimes( const std::vector<ForensicHashReportData>& data, FILETIME& startTime, FILETIME& endTime )
    {
        startTime = IsZeroFileTime(data[0].hashStartedUtc) ? data[0].hashedUtc : data[0].hashStartedUtc;
        endTime = data[0].hashedUtc;
        ULONGLONG startValue = FileTimeValue(startTime);
        ULONGLONG endValue = FileTimeValue(endTime);

        for (size_t i = 1; i < data.size(); ++i)
        {
            const FILETIME candidateStart = IsZeroFileTime(data[i].hashStartedUtc) ? data[i].hashedUtc : data[i].hashStartedUtc;
            const ULONGLONG candidateStartValue = FileTimeValue(candidateStart);
            const ULONGLONG candidateEndValue = FileTimeValue(data[i].hashedUtc);
            if (candidateStartValue < startValue)
            {
                startTime = candidateStart;
                startValue = candidateStartValue;
            }
            if (candidateEndValue > endValue)
            {
                endTime = data[i].hashedUtc;
                endValue = candidateEndValue;
            }
        }
    }

    CStringW DisplayDirectory( const ForensicHashReportData& item )
    {
        CStringW directory = DirectoryOnly(item.pathname);
        if (!directory.IsEmpty() && item.suppressUserProfile)
            directory = SuppressUserProfileName(directory);
        return directory;
    }

    std::vector<CStringW> SourcePathEntries( const std::vector<ForensicHashReportData>& data )
    {
        std::vector<CStringW> result;
        for (size_t i = 0; i < data.size(); ++i)
        {
            const CStringW directory = DisplayDirectory(data[i]);
            if (directory.IsEmpty())
                continue;

            bool alreadyPresent = false;
            for (size_t p = 0; p < result.size(); ++p)
            {
                if (result[p].CompareNoCase(directory) == 0)
                {
                    alreadyPresent = true;
                    break;
                }
            }
            if (!alreadyPresent)
                result.push_back(directory);
        }
        return result;
    }

    int PathIndicatorForItem( const ForensicHashReportData& item, const std::vector<CStringW>& paths )
    {
        const CStringW directory = DisplayDirectory(item);
        for (size_t i = 0; i < paths.size(); ++i)
        {
            if (paths[i].CompareNoCase(directory) == 0)
                return (int)i + 1;
        }
        return 0;
    }

    int PathListLineCount( const std::vector<ForensicHashReportData>& data )
    {
        const std::vector<CStringW> paths = SourcePathEntries(data);
        int lines = 0;
        for (size_t i = 0; i < paths.size(); ++i)
        {
            const std::vector<std::string> wrapped = Wrap(Narrow(paths[i]), 103);
            lines += (int)__max((size_t)1, wrapped.size());
        }
        return __max(1, lines);
    }

    int FirstPageTableTop( const std::vector<ForensicHashReportData>& data )
    {
        // The fixed metadata ends at Report Generated. Path entries are placed
        // immediately beneath it, one numbered path per list item, with wrapped
        // continuation lines indented. Reserve table space for those lines.
        const int pathLines = PathListLineCount(data);
        int top = 396 - (pathLines - 1) * 11;
        if (top < 285) top = 285;
        return top;
    }

    std::string PdfDateNow()
    {
        SYSTEMTIME st={0}; ::GetSystemTime(&st);
        char b[64]; sprintf_s(b, "D:%04u%02u%02u%02u%02u%02uZ", st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        return b;
    }

    struct PageLayout
    {
        size_t firstItem;
        size_t itemCount;
    };

    std::vector<PageLayout> Paginate( const std::vector<ForensicHashReportData>& data )
    {
        std::vector<PageLayout> pages;
        size_t item = 0;
        bool firstPage = true;
        while (item < data.size())
        {
            // First page reserves room for case/examiner/report metadata.
            int tableTop = firstPage ? FirstPageTableTop(data) : 558;
            int y = tableTop - TABLE_HEADER_HEIGHT;
            size_t start = item;
            while (item < data.size())
            {
                int height = RowHeight(data[item]);
                if (y - height < TABLE_BOTTOM && item > start)
                    break;
                // A single pathological row is still placed on the page and clipped only if it
                // exceeds the entire printable table area; normal Windows filenames/hashes fit.
                y -= height;
                ++item;
            }
            PageLayout page;
            page.firstItem = start;
            page.itemCount = item - start;
            pages.push_back(page);
            firstPage = false;
        }
        return pages;
    }

    void DrawReportMetadata( std::ostringstream& content, const std::vector<ForensicHashReportData>& data, const FILETIME& generated )
    {
        TextLine(content, "F2", 17, LEFT, 574, "CyoHash - Forensic Hash Report");
        TextLine(content, "F1", 8, LEFT, 559, "CyoHash v2.6.0");
        DrawLine(content, LEFT, 551, RIGHT, 551);

        const ForensicHashReportData& firstData = data[0];
        TextLine(content, "F2", 9, LEFT, 531, "Case Number/Name:");
        TextLine(content, "F1", 9, 132, 531, Narrow(firstData.caseName));
        TextLine(content, "F2", 9, 420, 531, "Evidence/Item:");
        TextLine(content, "F1", 9, 501, 531, firstData.evidenceItem.IsEmpty() ? "N/A" : Narrow(firstData.evidenceItem));

        // Keep examiner information directly beneath the case number so the
        // left metadata column reads naturally from case to examiner.
        TextLine(content, "F2", 9, LEFT, 513, "Examiner:");
        TextLine(content, "F1", 9, 88, 513, Narrow(firstData.examiner));

        FILETIME hashStart, hashEnd;
        HashingTimes(data, hashStart, hashEnd);

        TextLine(content, "F2", 9, LEFT, 495, "Hash Start Time:");
        TextLine(content, "F1", 9, 122, 495, Narrow(FormatLocal(hashStart)));

        TextLine(content, "F2", 9, LEFT, 477, "Hash End Time:");
        TextLine(content, "F1", 9, 122, 477, Narrow(FormatLocal(hashEnd)));

        TextLine(content, "F2", 9, LEFT, 459, "Report Generated:");
        TextLine(content, "F1", 9, 122, 459, Narrow(FormatLocal(generated)));
        TextLine(content, "F2", 9, 420, 459, "Results:");
        std::ostringstream count;
        count << data.size();
        TextLine(content, "F1", 9, 466, 459, count.str());

        // Numbered source paths provide a compact reference for the Path column.
        // Every report uses Path 1, Path 2, etc., including a single-path report,
        // so the table indicator has a consistent meaning.
        const std::vector<CStringW> paths = SourcePathEntries(data);
        int y = 438;
        for (size_t i = 0; i < paths.size(); ++i)
        {
            std::ostringstream label;
            label << "Path " << (i + 1) << ":";
            TextLine(content, "F2", 8, LEFT, y, label.str());

            const std::vector<std::string> wrapped = Wrap(Narrow(paths[i]), 103);
            for (size_t line = 0; line < wrapped.size(); ++line)
            {
                TextLine(content, "F1", 8, 82, y, wrapped[line]);
                y -= 11;
            }
        }
    }

    std::string BuildPageContent( const std::vector<ForensicHashReportData>& data, const std::vector<int>& matchGroups,
                                  const FILETIME& generated, const PageLayout& page, size_t pageIndex, size_t pageCount )
    {
        std::ostringstream content;
        int tableTop;
        if (pageIndex == 0)
        {
            DrawReportMetadata(content, data, generated);
            tableTop = FirstPageTableTop(data);
        }
        else
        {
            TextLine(content, "F2", 11, LEFT, 578, "CyoHash - Forensic Hash Report (continued)");
            tableTop = 558;
        }

        const std::vector<CStringW> sourcePaths = SourcePathEntries(data);
        DrawTableHeader(content, tableTop);
        int y = tableTop - TABLE_HEADER_HEIGHT;
        for (size_t n = 0; n < page.itemCount; ++n)
        {
            const size_t itemIndex = page.firstItem + n;
            const ForensicHashReportData& item = data[itemIndex];
            const int height = RowHeight(item);
            DrawRow(content, y, height, item, matchGroups[itemIndex], PathIndicatorForItem(item, sourcePaths));
            y -= height;
        }

        // Report legend: matching duplicate groups use the same pastel fill.
        // Keep this explanation in the footer on every page so the meaning of
        // highlighted rows remains clear even when a page is viewed separately.
        TextLine(content, "F1", 7, LEFT, FOOTER_Y,
                 "Like-colored highlights indicate matching hash values within the same algorithm.");

        std::ostringstream footer;
        footer << "Page " << (pageIndex + 1) << " of " << pageCount;
        TextLine(content, "F1", 8, 685, FOOTER_Y, footer.str());
        return content.str();
    }
}

bool pdfreport::SaveForensicHashReport( LPCWSTR pathname, const ForensicHashReportData& data, CStringW& error )
{
    std::vector<ForensicHashReportData> items;
    items.push_back(data);
    return SaveForensicHashReportBatch(pathname, items, error);
}

bool pdfreport::SaveForensicHashReportBatch( LPCWSTR pathname, const std::vector<ForensicHashReportData>& data, CStringW& error )
{
    try
    {
        if (data.empty())
        {
            error = L"No completed hash results were supplied for the PDF report.";
            return false;
        }

        FILETIME generated; ::GetSystemTimeAsFileTime(&generated);
        const std::vector<int> matchGroups = BuildMatchGroups(data);
        const std::vector<PageLayout> pages = Paginate(data);
        const int count = (int)pages.size();
        const int firstPageObj = 3;
        const int firstContentObj = firstPageObj + count;
        const int font1Obj = firstContentObj + count;
        const int font2Obj = font1Obj + 1;
        const int font3Obj = font2Obj + 1;
        const int infoObj = font3Obj + 1;
        const int objectCount = infoObj;

        std::vector<std::string> obj(objectCount + 1);
        obj[1] = "<< /Type /Catalog /Pages 2 0 R >>";

        std::ostringstream kids;
        kids << "<< /Type /Pages /Kids [";
        for (int i = 0; i < count; ++i)
            kids << (firstPageObj + i) << " 0 R ";
        kids << "] /Count " << count << " >>";
        obj[2] = kids.str();

        for (int i = 0; i < count; ++i)
        {
            const int pageObj = firstPageObj + i;
            const int contentObj = firstContentObj + i;
            std::ostringstream page;
            page << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << PAGE_WIDTH << " " << PAGE_HEIGHT << "] /Resources << /Font << "
                 << "/F1 " << font1Obj << " 0 R /F2 " << font2Obj << " 0 R /F3 " << font3Obj
                 << " 0 R >> >> /Contents " << contentObj << " 0 R >>";
            obj[pageObj] = page.str();

            std::string stream = BuildPageContent(data, matchGroups, generated, pages[i], i, pages.size());
            std::ostringstream contentObjText;
            contentObjText << "<< /Length " << stream.size() << " >>\nstream\n" << stream << "endstream";
            obj[contentObj] = contentObjText.str();
        }

        obj[font1Obj] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
        obj[font2Obj] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>";
        obj[font3Obj] = "<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>";
        obj[infoObj] = std::string("<< /Title (CyoHash Forensic Hash Report) /Creator (CyoHash v2.6.0) /CreationDate (") + PdfDateNow() + ") >>";

        std::ofstream file(pathname, std::ios::binary | std::ios::trunc);
        if (!file) { error=L"Unable to create the PDF file."; return false; }
        file << "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::vector<std::streamoff> offsets(objectCount + 1);
        for (int i = 1; i <= objectCount; ++i)
        {
            offsets[i] = file.tellp();
            file << i << " 0 obj\n" << obj[i] << "\nendobj\n";
        }
        std::streamoff xref=file.tellp();
        file << "xref\n0 " << (objectCount + 1) << "\n0000000000 65535 f \n";
        for (int i = 1; i <= objectCount; ++i)
            file << std::setw(10) << std::setfill('0') << (long long)offsets[i] << " 00000 n \n";
        file << "trailer\n<< /Size " << (objectCount + 1) << " /Root 1 0 R /Info " << infoObj << " 0 R >>\nstartxref\n"
             << (long long)xref << "\n%%EOF\n";
        file.close();
        if (!file) { error=L"An error occurred while writing the PDF file."; return false; }
        return true;
    }
    catch (const std::exception& ex)
    {
        CA2W msg(ex.what()); error=msg; return false;
    }
}
