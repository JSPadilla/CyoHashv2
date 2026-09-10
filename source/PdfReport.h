#pragma once

struct ForensicHashReportData
{
    CStringW caseName;
    CStringW examiner;
    CStringW evidenceItem;
    CStringW pathname;
    CStringW algorithm;
    CStringW hash;
    ULONGLONG fileSize;
    FILETIME hashStartedUtc;
    FILETIME hashedUtc;
    bool suppressUserProfile;
};

namespace pdfreport
{
    bool SaveForensicHashReport( LPCWSTR pathname, const ForensicHashReportData& data, CStringW& error );
    bool SaveForensicHashReportBatch( LPCWSTR pathname, const std::vector<ForensicHashReportData>& data, CStringW& error );
}
