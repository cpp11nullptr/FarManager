#include "7z.h"
#include <objbase.h>

CArchiveExtractCallback::CArchiveExtractCallback(
		SevenZipArchive* pArchive,
		ArchiveItemEx *pItems,
		int nItemsNumber,
		const TCHAR *lpDestDiskPath,
		const TCHAR *lpPathInArchive
		)
{
	m_pArchive = pArchive;

	m_nRefCount = 1;

	m_pItems = pItems;
	m_nItemsNumber = nItemsNumber;

	m_strDestDiskPath = lpDestDiskPath;
	m_strPathInArchive = lpPathInArchive;

	m_pGetTextPassword = NULL;

	m_bUserAbort = false;
	m_nSuccessCount = 0;
	m_bExtractMode = false;

	///???
	m_pArchive->OnStartOperation(OPERATION_EXTRACT, 0, 0);
}


CArchiveExtractCallback::~CArchiveExtractCallback()
{
	if ( m_pGetTextPassword )
		m_pGetTextPassword->Release();
}

ULONG __stdcall CArchiveExtractCallback::AddRef()
{
	return ++m_nRefCount;
}

ULONG __stdcall CArchiveExtractCallback::Release()
{
	if ( --m_nRefCount == 0 )
	{
		delete this;
		return 0;
	}

	return m_nRefCount;
}


HRESULT __stdcall CArchiveExtractCallback::QueryInterface(const IID &iid, void ** ppvObject)
{
	*ppvObject = NULL;

	if ( iid == IID_IArchiveExtractCallback )
	{
		*ppvObject = this;
		AddRef ();

		return S_OK;
	}

	if ( iid == IID_ICryptoGetTextPassword )
	{
		if ( !m_pGetTextPassword )
			m_pGetTextPassword = new CCryptoGetTextPassword(m_pArchive, TYPE_FILE);

		m_pGetTextPassword->AddRef ();
		*ppvObject = m_pGetTextPassword; //????

		return S_OK;
	}


	return E_NOINTERFACE;
}


HRESULT __stdcall CArchiveExtractCallback::SetTotal(unsigned __int64 total)
{
	m_uProcessedBytes = (unsigned __int64)-1;
	return S_OK;
}

HRESULT CArchiveExtractCallback::SetCompleted(const unsigned __int64* completeValue)
{
	if ( m_uProcessedBytes != (unsigned __int64)-1 )
	{
		unsigned __int64 diff = *completeValue-m_uProcessedBytes;

		if ( !m_pArchive->OnProcessData(diff) )
			return E_ABORT;

		m_uProcessedBytes = *completeValue;
	}

	return S_OK;
}



int GetItemIndex(CArchiveExtractCallback *pcb, int index)
{
	for (int i = 0; i < pcb->m_nItemsNumber; i++)
	{
		if ( (int)pcb->m_pItems[i].nIndex == index )
			return i;
	}

	return -1;
}



HRESULT __stdcall CArchiveExtractCallback::GetStream(
		unsigned int index,
		ISequentialOutStream** outStream,
		int askExtractMode
		)

{
	CPropVariant value;

	IInArchive *pArchive = m_pArchive->GetArchive();

	if ( askExtractMode == 0 ) //extract
	{
		if ( pArchive->GetProperty(index, kpidPath, &value) != S_OK )
			return S_OK; //!!! to return error

		string strArcFileName;
		string strFullName;

		if ( value.vt == VT_BSTR )
		{
#ifdef UNICODE
			strArcFileName = value.bstrVal;
#else
			strArcFileName.SetData(value.bstrVal, CP_OEMCP);
#endif
		}
		else
		{
			strArcFileName = FSF.PointToName (m_pArchive->GetFileName());
			CutTo (strArcFileName, _T('.'), true);
		}

		strFullName = m_strDestDiskPath;

		if ( !FSF.LStrnicmp (strArcFileName, m_strPathInArchive, m_strPathInArchive.GetLength()) )
			strFullName += (const TCHAR*)strArcFileName+m_strPathInArchive.GetLength(); //FIX ASAP!!!
		else
			strFullName += strArcFileName;

		int itemindex = GetItemIndex(this, index);
		const ArchiveItem* item = m_pItems[itemindex].pItem;

		int nOverwrite = m_pArchive->OnProcessFile(item, strFullName);

		if ( nOverwrite == PROCESS_CANCEL )
		{
			m_bUserAbort = true;

			*outStream = NULL;
			return S_OK;
		}

		if ( nOverwrite == PROCESS_SKIP )
		{
			m_nSuccessCount++;

			*outStream = NULL;
			return S_OK;
		}

		//� �� �� �� �।?
		if ( m_uProcessedBytes == (unsigned __int64)-1 )
			m_uProcessedBytes = 0;

		FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
		DWORD dwFileAttributes = 0;

		memset (&ftCreationTime, 0, sizeof (FILETIME));
		memset (&ftLastAccessTime, 0, sizeof (FILETIME));
		memset (&ftLastWriteTime, 0, sizeof (FILETIME));

		if ( pArchive->GetProperty(index, kpidAttrib, &value) == S_OK )
		{
			if ( value.vt == VT_UI4 )
			dwFileAttributes = value.ulVal;
		}

		if ( pArchive->GetProperty(index, kpidCTime, &value) == S_OK )
		{
			if ( value.vt == VT_FILETIME )
				memcpy (&ftCreationTime, &value.filetime, sizeof (FILETIME));
		}

		if ( pArchive->GetProperty(index, kpidATime, &value) == S_OK )
		{
			if ( value.vt == VT_FILETIME )
				memcpy (&ftLastAccessTime, &value.filetime, sizeof (FILETIME));
		}

		if ( pArchive->GetProperty(index, kpidMTime, &value) == S_OK )
		{
			if ( value.vt == VT_FILETIME )
				memcpy (&ftLastWriteTime, &value.filetime, sizeof (FILETIME));
		}

		bool bIsFolder = false;

		if ( pArchive->GetProperty(index, kpidIsDir, &value) == S_OK )
		{
			if (value.vt == VT_BOOL)
				bIsFolder = (value.boolVal == VARIANT_TRUE);
		}

		if ( bIsFolder ||
			 OptionIsOn (dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY) )//||
			 //OptionIsOn (item->FindData.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY) )
		{
			*outStream = NULL;
			apiCreateDirectoryEx(strFullName); //, dwFileAttributes);
		}
		else
		{
			apiCreateDirectoryForFile(strFullName);

			COutFile *file = new COutFile(strFullName);

			if ( file->Open() )
			{
				file->SetAttributes(dwFileAttributes);
				file->SetTime(&ftCreationTime, &ftLastAccessTime, &ftLastWriteTime);
				*outStream = file;
			}
			else
				delete file;
		}
	}
	else
		*outStream = NULL;

	return S_OK;
}

HRESULT __stdcall CArchiveExtractCallback::PrepareOperation(int askExtractMode)
{
	if ( askExtractMode == 0 )
		m_bExtractMode = true;

	return S_OK;
}

HRESULT __stdcall CArchiveExtractCallback::SetOperationResult(int resultEOperationResult)
{
	FarMessage msg(FMSG_WARNING);

	switch( resultEOperationResult )
	{
		case NArchive::NExtract::NOperationResult::kOK:
			
			if ( m_bExtractMode )
				m_nSuccessCount++;

			return S_OK;

		case NArchive::NExtract::NOperationResult::kDataError:

			//remove to callback if possible
			msg.Add(_T("File data error"));
			msg.AddButton(_T("Ok"));

			msg.Run();

			return S_OK; //??

		case NArchive::NExtract::NOperationResult::kCRCError:
			m_pArchive->OnPasswordOperation(PASSWORD_RESET, NULL, 0);

			//remove to callback if possible
			msg.Add(_T("File CRC error"));
			msg.AddButton(_T("Ok"));

			msg.Run();
			
			return S_OK; //??
	}

	return S_OK;
}

int CArchiveExtractCallback::GetResult()
{
	if ( m_bUserAbort )
		return RESULT_CANCEL;

	if ( m_nSuccessCount > m_nItemsNumber )
		__debug(_T("LOGIC ERROR, PLEASE REPORT"));

	if ( m_nSuccessCount == 0 )
		return RESULT_ERROR;

	if ( m_nSuccessCount < m_nItemsNumber )
		return RESULT_PARTIAL;

	return RESULT_SUCCESS;
}

