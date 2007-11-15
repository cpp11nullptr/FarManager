/*
filefilter.cpp

�������� ������

*/

#include "headers.hpp"
#pragma hdrstop

#include "global.hpp"
#include "fn.hpp"
#include "lang.hpp"
#include "keys.hpp"
#include "ctrlobj.hpp"
#include "filepanels.hpp"
#include "panel.hpp"
#include "vmenu.hpp"
#include "scantree.hpp"
#include "filefilter.hpp"
#include "array.hpp"
#include "filelist.hpp"

static int _cdecl ExtSort(const void *el1,const void *el2);

static TPointerArray<FileFilterParams> FilterData, TempFilterData;
static FileFilterParams FoldersFilter;
static BitFlags FolderFlags; //����� ��� ���������� ���������� �����

FileFilter::FileFilter(Panel *HostPanel, enumFileFilterType FilterType)
{
  m_HostPanel=HostPanel;
  m_FilterType=FilterType;
}

FileFilter::~FileFilter()
{
}

bool FileFilter::FilterEdit()
{
  struct MenuItem ListItem;
  int ExitCode;
  bool bNeedUpdate=false;
  VMenu FilterList("",NULL,0,ScrY-6);

  {
    DWORD Inc,Exc;
    GetIncludeExcludeFlags(Inc,Exc);
    if (FolderFlags.Check(Inc))
      FilterList.SetTitle(MSG(MFilterTitle_IncFolders));
    else if (FolderFlags.Check(Exc))
      FilterList.SetTitle(MSG(MFilterTitle_ExcFolders));
    else
      FilterList.SetTitle(MSG(MFilterTitle_FilterFolders));
  }

  FilterList.SetHelp("FiltersMenu");
  FilterList.SetPosition(-1,-1,0,0);
  FilterList.SetBottomTitle(MSG(MFilterBottom));
  FilterList.SetFlags(VMENU_SHOWAMPERSAND|VMENU_WRAPMODE);

  for (unsigned int i=0; i<FilterData.getCount(); i++)
  {
    memset(&ListItem,0,sizeof(ListItem));
    MenuString(ListItem.Name,FilterData.getItem(i));

    if(i == 0)
      ListItem.Flags|=LIF_SELECTED;

    int Check = GetCheck(FilterData.getItem(i));
    if (Check)
      ListItem.SetCheck(Check);

    FilterList.AddItem(&ListItem);
  }

  memset(&ListItem,0,sizeof(ListItem));
  if (FilterData.getCount()==0)
    ListItem.Flags|=LIF_SELECTED;
  FilterList.AddItem(&ListItem);

  char *ExtPtr=NULL;
  int ExtCount=0;

  {
    DWORD Inc,Exc;
    GetIncludeExcludeFlags(Inc,Exc);
    for (unsigned int i=0; i<TempFilterData.getCount(); i++)
    {
      //AY: ����� ���������� ������ �� ��������� ���� �������
      //(��� ������� ���� ������ �� ������) ������� ������� � ������� ������� ����
      if (!TempFilterData.getItem(i)->Flags.Check(Inc|Exc))
        continue;
      const char *FMask;
      TempFilterData.getItem(i)->GetMask(&FMask);
      char Mask[FILEFILTER_MASK_SIZE];
      xstrncpy(Mask,FMask,sizeof(Mask)-1);
      Unquote(Mask);
      if(!ParseAndAddMasks(&ExtPtr,Mask,0,ExtCount,GetCheck(TempFilterData.getItem(i))))
        break;
    }
  }

  memset(&ListItem,0,sizeof(ListItem));
  ListItem.Flags|=LIF_SEPARATOR;
  FilterList.AddItem(&ListItem);

  memset(&ListItem,0,sizeof(ListItem));
  FoldersFilter.SetTitle(MSG(MFolderFileType));
  MenuString(ListItem.Name,&FoldersFilter);
  int Check = GetCheck(&FoldersFilter);
  if (Check)
    ListItem.SetCheck(Check);
  FilterList.AddItem(&ListItem);

  if (m_HostPanel->GetMode()==NORMAL_PANEL)
  {
    char CurDir[NM];
    char FileName[NM];
    WIN32_FIND_DATA fdata;
    m_HostPanel->GetCurDir(CurDir);

    ScanTree ScTree(FALSE,FALSE);
    ScTree.SetFindPath(CurDir,"*.*");
    while (ScTree.GetNextName(&fdata,FileName, sizeof (FileName)-1))
      if(!ParseAndAddMasks(&ExtPtr,fdata.cFileName,fdata.dwFileAttributes,ExtCount,0))
        break;
  }
  else
  {
    char FileName[NM];
    int FileAttr;
    for (int i=0; m_HostPanel->GetFileName(FileName,i,FileAttr); i++)
      if(!ParseAndAddMasks(&ExtPtr,FileName,FileAttr,ExtCount,0))
        break;
  }

  far_qsort((void *)ExtPtr,ExtCount,NM,ExtSort);

  memset(&ListItem,0,sizeof(ListItem));

  for (int i=0; i<ExtCount; i++)
  {
    char *CurExtPtr=ExtPtr+i*NM;
    MenuString(ListItem.Name,NULL,false,true,CurExtPtr,MSG(MPanelFileType));
    ListItem.SetCheck(CurExtPtr[strlen(CurExtPtr)+1]);
    FilterList.SetUserData(CurExtPtr,0,FilterList.AddItem(&ListItem));
  }
  xf_free(ExtPtr);

  FilterList.Show();

  while (!FilterList.Done())
  {
    int Key=FilterList.ReadInput();

    if (Key==KEY_ADD)
      Key='+';
    else if (Key==KEY_SUBTRACT)
      Key='-';

    switch(Key)
    {
      case KEY_SPACE:
      case '+':
      case '-':
      case KEY_BS:
      {
        int SelPos=FilterList.GetSelectPos();

        if (SelPos==FilterData.getCount())
          break;

        int Check=FilterList.GetSelection(SelPos),NewCheck;
        if (Key=='-')
          NewCheck=(Check=='-') ? 0:'-';
        else if (Key=='+')
          NewCheck=(Check=='+') ? 0:'+';
        else if (Key==KEY_BS)
          NewCheck=0;
        else
          NewCheck=Check ? 0:'+';

        FilterList.SetSelection(NewCheck,SelPos);
        FilterList.SetSelectPos(SelPos,1);
        FilterList.SetUpdateRequired(TRUE);
        FilterList.FastShow();
        FilterList.ProcessKey(KEY_DOWN);
        break;
      }

      case KEY_SHIFTSUBTRACT:
      case KEY_SHIFTBS:
      {
        for (int I=0; I < FilterList.GetItemCount(); I++)
        {
          FilterList.SetSelection(FALSE, I);
        }
        FilterList.SetUpdateRequired(TRUE);
        FilterList.FastShow();
        if (Key!=KEY_SHIFTSUBTRACT)
          break;
      }
      case KEY_CTRLF:
      {
        DWORD Inc,Exc;
        GetIncludeExcludeFlags(Inc,Exc);
        if (Key==KEY_CTRLF)
        {
          if (m_FilterType == FFT_SELECT)
            FolderFlags.Swap(Exc);
          else
            FolderFlags.Swap(Inc);
        }
        else
        {
          if (m_FilterType == FFT_SELECT)
            FolderFlags.Set(Exc);
          else
            FolderFlags.Set(Inc);
        }
        if (FolderFlags.Check(Inc))
          FilterList.SetTitle(MSG(MFilterTitle_IncFolders));
        else if (FolderFlags.Check(Exc))
          FilterList.SetTitle(MSG(MFilterTitle_ExcFolders));
        else
          FilterList.SetTitle(MSG(MFilterTitle_FilterFolders));
        FilterList.SetUpdateRequired(TRUE);
        FilterList.SetPosition(-1,-1,0,0);
        FilterList.Show();
        bNeedUpdate=true;
        break;
      }

      case KEY_F4:
      {
        int SelPos=FilterList.GetSelectPos();
        if (SelPos<(int)FilterData.getCount())
        {
          if (FileFilterConfig(FilterData.getItem(SelPos)))
          {
            memset(&ListItem,0,sizeof(ListItem));
            MenuString(ListItem.Name,FilterData.getItem(SelPos));
            int Check = GetCheck(FilterData.getItem(SelPos));
            if (Check)
              ListItem.SetCheck(Check);

            FilterList.DeleteItem(SelPos);
            FilterList.AddItem(&ListItem,SelPos);

            FilterList.AdjustSelectPos();
            FilterList.SetSelectPos(SelPos,1);
            FilterList.SetUpdateRequired(TRUE);
            FilterList.FastShow();
            bNeedUpdate=true;
          }
        }
        else if (SelPos>(int)FilterData.getCount())
        {
          Message(MSG_WARNING,1,MSG(MFilterTitle),MSG(MCanEditCustomFilterOnly),MSG(MOk));
        }
        break;
      }

      case KEY_INS:
      case KEY_F5:
      {
        int SelPos=FilterList.GetSelectPos();
        int SelPos2=SelPos+1;

        if (SelPos>(int)FilterData.getCount())
          SelPos=FilterData.getCount();

        FileFilterParams *NewFilter = FilterData.insertItem(SelPos);
        if (!NewFilter)
          break;

        if (Key==KEY_F5)
        {
          if (SelPos2 < (int)FilterData.getCount())
          {
            *NewFilter = *FilterData.getItem(SelPos2);

            NewFilter->SetTitle("");
            NewFilter->Flags.ClearAll();
          }
          else if (SelPos2 == (FilterData.getCount()+2))
          {
            *NewFilter = FoldersFilter;

            NewFilter->SetTitle("");
            NewFilter->Flags.ClearAll();
          }
          else if (SelPos2 > (int)(FilterData.getCount()+2))
          {
            char Mask[NM];
            FilterList.GetUserData(Mask,sizeof(Mask),SelPos2-1);

            NewFilter->SetMask(1,Mask);
            //���� ������� ��� ������ ��� ������, ����� �� ������ � ��� ���������
            NewFilter->SetAttr(1,0,FILE_ATTRIBUTE_DIRECTORY);
          }
          else
          {
            FilterData.deleteItem(SelPos);
            break;
          }
        }
        else
        {
          //AY: ��� ������ ����� ������ �� ����� ����� ������� ���� �� ����� ������ ��� ������
          NewFilter->SetAttr(1,0,FILE_ATTRIBUTE_DIRECTORY);
        }

        if (FileFilterConfig(NewFilter))
        {
          memset(&ListItem,0,sizeof(ListItem));
          MenuString(ListItem.Name,NewFilter);

          FilterList.AddItem(&ListItem,SelPos);

          FilterList.AdjustSelectPos();
          FilterList.SetSelectPos(SelPos,1);
          FilterList.SetPosition(-1,-1,0,0);
          FilterList.Show();
          bNeedUpdate=true;
        }
        else
          FilterData.deleteItem(SelPos);

        break;
      }

      case KEY_NUMDEL:
      case KEY_DEL:
      {
        int SelPos=FilterList.GetSelectPos();
        if (SelPos<(int)FilterData.getCount())
        {
          char QuotedTitle[512+2];
          sprintf(QuotedTitle,"\"%.*s\"",sizeof(QuotedTitle)-1-2,FilterData.getItem(SelPos)->GetTitle());
          if (Message(0,2,MSG(MFilterTitle),MSG(MAskDeleteFilter),
                      QuotedTitle,MSG(MDelete),MSG(MCancel))==0)
          {
            FilterData.deleteItem(SelPos);

            FilterList.DeleteItem(SelPos);

            FilterList.AdjustSelectPos();
            FilterList.SetSelectPos(SelPos,1);
            FilterList.SetPosition(-1,-1,0,0);
            FilterList.Show();
            bNeedUpdate=true;
          }
        }
        else if (SelPos>(int)FilterData.getCount())
        {
          Message(MSG_WARNING,1,MSG(MFilterTitle),MSG(MCanDeleteCustomFilterOnly),MSG(MOk));
        }
        break;
      }

      case KEY_CTRLUP:
      case KEY_CTRLDOWN:
      {
        int SelPos=FilterList.GetSelectPos();
        if (SelPos<(int)FilterData.getCount() && !(Key==KEY_CTRLUP && SelPos==0) && !(Key==KEY_CTRLDOWN && SelPos==FilterData.getCount()-1))
        {
          int NewPos = SelPos + (Key == KEY_CTRLDOWN ? 1 : -1);
          MenuItem CurItem, NextItem;
          memcpy(&CurItem,FilterList.GetItemPtr(SelPos),sizeof(CurItem));
          memcpy(&NextItem,FilterList.GetItemPtr(NewPos),sizeof(NextItem));

          FilterData.swapItems(NewPos,SelPos);

          if (NewPos<SelPos)
          {
            FilterList.DeleteItem(NewPos,2);
            FilterList.AddItem(&CurItem,NewPos);
            FilterList.AddItem(&NextItem,SelPos);
          }
          else
          {
            FilterList.DeleteItem(SelPos,2);
            FilterList.AddItem(&NextItem,SelPos);
            FilterList.AddItem(&CurItem,NewPos);
          }

          FilterList.AdjustSelectPos();
          FilterList.SetSelectPos(NewPos,1);
          FilterList.SetUpdateRequired(TRUE);
          FilterList.FastShow();
          bNeedUpdate=true;
        }
        break;
      }

      default:
      {
        FilterList.ProcessInput();
      }
    }
  }

  ExitCode=FilterList.Modal::GetExitCode();

  if (ExitCode!=-1)
    ProcessSelection(&FilterList);

  if(Opt.AutoSaveSetup)
    SaveFilters();

  if (ExitCode!=-1 || bNeedUpdate)
  {
    if (m_FilterType == FFT_PANEL)
    {
      m_HostPanel->Update(UPDATE_KEEP_SELECTION);
      m_HostPanel->Redraw();
    }
  }

  return (ExitCode!=-1);
}

void FileFilter::GetIncludeExcludeFlags(DWORD &Inc, DWORD &Exc)
{
  if (m_FilterType == FFT_PANEL)
  {
    if (m_HostPanel==CtrlObject->Cp()->RightPanel)
    {
      Inc = FFF_RPANELINCLUDE;
      Exc = FFF_RPANELEXCLUDE;
    }
    else
    {
      Inc = FFF_LPANELINCLUDE;
      Exc = FFF_LPANELEXCLUDE;
    }
  }
  else if (m_FilterType == FFT_COPY)
  {
    Inc = FFF_COPYINCLUDE;
    Exc = FFF_COPYEXCLUDE;
  }
  else if (m_FilterType == FFT_FINDFILE)
  {
    Inc = FFF_FINDFILEINCLUDE;
    Exc = FFF_FINDFILEEXCLUDE;
  }
  else
  {
    Inc = FFF_SELECTINCLUDE;
    Exc = FFF_SELECTEXCLUDE;
  }
}

int FileFilter::GetCheck(FileFilterParams *FFP)
{
  DWORD Inc,Exc;
  GetIncludeExcludeFlags(Inc,Exc);

  if (FFP->Flags.Check(Inc))
    return '+';
  else if (FFP->Flags.Check(Exc))
    return '-';

  return 0;
}

void FileFilter::ProcessSelection(VMenu *FilterList)
{
  DWORD Inc,Exc;
  GetIncludeExcludeFlags(Inc,Exc);

  FileFilterParams *CurFilterData;
  for (int i=0,j=0; i < FilterList->GetItemCount(); i++)
  {
    int Check=FilterList->GetSelection(i);

    CurFilterData=NULL;

    if (i < (int)FilterData.getCount())
    {
      CurFilterData = FilterData.getItem(i);
    }
    else if (i == (FilterData.getCount() + 2))
    {
      CurFilterData = &FoldersFilter;
    }
    else if (i > (int)(FilterData.getCount() + 2))
    {
      const char *FMask;
      char Mask[NM], Mask1[NM];
      FilterList->GetUserData(Mask,sizeof(Mask),i);

      //AY: ��� ��� � ���� �� ���������� ������ �� ��������� ���� �������
      //������� ������� � ������� ������� ���� � TempFilterData ������
      //����� ��������� ����� ������� ���� ��� ������� � ���� ���� ��
      //��� ��� ���� ������� � ������ � ��� ��� TempFilterData
      //� ���� ������� � ���� ������������� �� �������� �� �������
      //��������� ���� �� ���� ���������� � ������.
      xstrncpy(Mask1,Mask,sizeof(Mask1)-1);
      Unquote(Mask1);
      while ((CurFilterData=TempFilterData.getItem(j))!=NULL)
      {
        char Mask2[FILEFILTER_MASK_SIZE];
        CurFilterData->GetMask(&FMask);
        xstrncpy(Mask2,FMask,sizeof(Mask2)-1);
        Unquote(Mask2);
        if (LocalStricmp(Mask1,Mask2)<1)
          break;
        j++;
      }

      if (CurFilterData)
      {
        if (!LocalStricmp(Mask,FMask))
        {
          if (!Check && !CurFilterData->Flags.Check(~(Inc|Exc)))
          {
            TempFilterData.deleteItem(j);
            continue;
          }
          else
            j++;
        }
        else
          CurFilterData=NULL;
      }

      if (Check && !CurFilterData)
      {
        FileFilterParams *NewFilter = TempFilterData.insertItem(j);

        if (NewFilter)
        {
          NewFilter->SetMask(1,Mask);
          //���� ������� ��� ������ ��� ������, ����� �� ������ � ��� ���������
          NewFilter->SetAttr(1,0,FILE_ATTRIBUTE_DIRECTORY);

          j++;
          CurFilterData = NewFilter;
        }
        else
          continue;
      }
    }

    if (!CurFilterData)
      continue;

    CurFilterData->Flags.Clear(Inc|Exc);
    if (Check=='+')
      CurFilterData->Flags.Set(Inc);
    else if (Check=='-')
      CurFilterData->Flags.Set(Exc);
  }
}

bool FileFilter::FileInFilter(FileListItem *fli)
{
  WIN32_FIND_DATA fd;

  fd.dwFileAttributes=fli->FileAttr;
  fd.ftCreationTime=fli->CreationTime;
  fd.ftLastAccessTime=fli->AccessTime;
  fd.ftLastWriteTime=fli->WriteTime;
  fd.nFileSizeHigh=fli->UnpSizeHigh;
  fd.nFileSizeLow=fli->UnpSize;
  fd.dwReserved0=fli->PackSizeHigh;
  fd.dwReserved1=fli->PackSize;
  xstrncpy(fd.cFileName,fli->Name,sizeof(fd.cFileName)-1);
  xstrncpy(fd.cAlternateFileName,fli->ShortName,sizeof(fd.cAlternateFileName)-1);

  return FileInFilter(&fd);
}

bool FileFilter::FileInFilter(WIN32_FIND_DATA *fd, bool IsExcludeDir)
{
  DWORD Inc,Exc;
  GetIncludeExcludeFlags(Inc,Exc);

  if ((fd->dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !IsExcludeDir)
  {
    if (FolderFlags.Check(Inc))
      return true;
    if (FolderFlags.Check(Exc))
      return false;
  }

  bool flag=false;
  bool bInc=false;
  bool bExc=false;
  FileFilterParams *CurFilterData;

  for (unsigned int i=0; i<FilterData.getCount(); i++)
  {
    CurFilterData = FilterData.getItem(i);

    if (CurFilterData->Flags.Check(Inc|Exc))
    {
      flag = flag || CurFilterData->Flags.Check(Inc);
      if (CurFilterData->FileInFilter(fd))
        CurFilterData->Flags.Check(Inc)?bInc=true:bExc=true;
    }
  }

  if (FoldersFilter.Flags.Check(Inc|Exc))
  {
    flag = flag || FoldersFilter.Flags.Check(Inc);
    if (FoldersFilter.FileInFilter(fd))
      FoldersFilter.Flags.Check(Inc)?bInc=true:bExc=true;
  }

  for (unsigned int i=0; i<TempFilterData.getCount(); i++)
  {
    CurFilterData = TempFilterData.getItem(i);

    if (CurFilterData->Flags.Check(Inc|Exc))
    {
      flag = flag || CurFilterData->Flags.Check(Inc);
      if (CurFilterData->FileInFilter(fd))
        CurFilterData->Flags.Check(Inc)?bInc=true:bExc=true;
    }
  }

  if(IsExcludeDir) return bExc;

  if (bExc) return false;
  if (bInc) return true;
  return !flag;
}

bool FileFilter::IsEnabledOnPanel()
{
  if (m_FilterType != FFT_PANEL)
    return false;

  DWORD Inc,Exc;
  GetIncludeExcludeFlags(Inc,Exc);

  for (unsigned int i=0; i<FilterData.getCount(); i++)
  {
    if (FilterData.getItem(i)->Flags.Check(Inc|Exc))
      return true;
  }

  if (FoldersFilter.Flags.Check(Inc|Exc))
    return true;

  for (unsigned int i=0; i<TempFilterData.getCount(); i++)
  {
    if (TempFilterData.getItem(i)->Flags.Check(Inc|Exc))
      return true;
  }

  return false;
}

void FileFilter::InitFilter()
{
  FilterData.Free();
  TempFilterData.Free();

  char RegKey[80], *PtrRegKey;

  strcpy(RegKey,"Filters\\Filter");
  PtrRegKey=RegKey+strlen(RegKey);

  while (1)
  {
    itoa(FilterData.getCount(),PtrRegKey,10);

    char Title[512];
    if (!GetRegKey(RegKey,"Title",Title,"",sizeof(Title)))
      break;

    FileFilterParams *NewFilter = FilterData.addItem();

    if (NewFilter)
    {
      //��������� �������� ������� ��� ���� ��� ����� ���������� ���������
      //��������� ������ ������ ����.

      NewFilter->SetTitle(Title);

      char Mask[FILEFILTER_MASK_SIZE];
      GetRegKey(RegKey,"Mask",Mask,"",sizeof(Mask));
      NewFilter->SetMask((DWORD)GetRegKey(RegKey,"UseMask",1),
                         Mask);

      FILETIME DateAfter, DateBefore;
      GetRegKey(RegKey,"DateAfter",(BYTE *)&DateAfter,NULL,sizeof(DateAfter));
      GetRegKey(RegKey,"DateBefore",(BYTE *)&DateBefore,NULL,sizeof(DateBefore));
      NewFilter->SetDate((DWORD)GetRegKey(RegKey,"UseDate",0),
                         (DWORD)GetRegKey(RegKey,"DateType",0),
                         DateAfter,
                         DateBefore);

      NewFilter->SetSize((DWORD)GetRegKey(RegKey,"UseSize",0),
                         (DWORD)GetRegKey(RegKey,"SizeType",0),
                         GetRegKey64(RegKey,"SizeAbove",(unsigned __int64)_i64(-1)),
                         GetRegKey64(RegKey,"SizeBelow",(unsigned __int64)_i64(-1)));

      NewFilter->SetAttr((DWORD)GetRegKey(RegKey,"UseAttr",1),
                         (DWORD)GetRegKey(RegKey,"AttrSet",0),
                         (DWORD)GetRegKey(RegKey,"AttrClear",FILE_ATTRIBUTE_DIRECTORY));

      NewFilter->Flags.Set((DWORD)GetRegKey(RegKey,"Flags",0));
    }
    else
      break;
  }

  strcpy(RegKey,"Filters\\PanelMask");
  PtrRegKey=RegKey+strlen(RegKey);

  while (1)
  {
    itoa(TempFilterData.getCount(),PtrRegKey,10);

    char Mask[FILEFILTER_MASK_SIZE];
    if (!GetRegKey(RegKey,"Mask",Mask,"",sizeof(Mask)))
      break;

    FileFilterParams *NewFilter = TempFilterData.addItem();

    if (NewFilter)
    {
      NewFilter->SetMask(1,Mask);
      //���� ������� ��� ������ ��� ������, ����� �� ������ � ��� ���������
      NewFilter->SetAttr(1,0,FILE_ATTRIBUTE_DIRECTORY);

      NewFilter->Flags.Set((DWORD)GetRegKey(RegKey,"Flags",0));
    }
    else
      break;
  }

  FoldersFilter.SetMask(0,"");
  FoldersFilter.SetAttr(1,FILE_ATTRIBUTE_DIRECTORY,0);
  FoldersFilter.Flags.Set((DWORD)GetRegKey("Filters","FoldersFilterFlags",0));

  FolderFlags.Set((DWORD)GetRegKey("Filters","FolderFlags",FFF_RPANELINCLUDE|FFF_LPANELINCLUDE|FFF_FINDFILEINCLUDE|FFF_COPYINCLUDE|FFF_SELECTEXCLUDE));
}


void FileFilter::CloseFilter()
{
  FilterData.Free();
  TempFilterData.Free();
}

void FileFilter::SaveFilters()
{
  char RegKey[80], *PtrRegKey;

  DeleteKeyTree("Filters");

  strcpy(RegKey,"Filters\\Filter");
  PtrRegKey=RegKey+strlen(RegKey);

  FileFilterParams *CurFilterData;

  for (unsigned int i=0; i<FilterData.getCount(); i++)
  {
    CurFilterData = FilterData.getItem(i);
    itoa(i,PtrRegKey,10);

    SetRegKey(RegKey,"Title",CurFilterData->GetTitle());

    const char *Mask;
    SetRegKey(RegKey,"UseMask",CurFilterData->GetMask(&Mask));
    SetRegKey(RegKey,"Mask",Mask);


    DWORD DateType;
    FILETIME DateAfter, DateBefore;
    SetRegKey(RegKey,"UseDate",CurFilterData->GetDate(&DateType, &DateAfter, &DateBefore));
    SetRegKey(RegKey,"DateType",DateType);
    SetRegKey(RegKey,"DateAfter",(BYTE *)&DateAfter,sizeof(DateAfter));
    SetRegKey(RegKey,"DateBefore",(BYTE *)&DateBefore,sizeof(DateBefore));


    DWORD SizeType;
    __int64 SizeAbove, SizeBelow;
    SetRegKey(RegKey,"UseSize",CurFilterData->GetSize(&SizeType, &SizeAbove, &SizeBelow));
    SetRegKey(RegKey,"SizeType",SizeType);
    SetRegKey64(RegKey,"SizeAbove",SizeAbove);
    SetRegKey64(RegKey,"SizeBelow",SizeBelow);


    DWORD AttrSet, AttrClear;
    SetRegKey(RegKey,"UseAttr",CurFilterData->GetAttr(&AttrSet, &AttrClear));
    SetRegKey(RegKey,"AttrSet",AttrSet);
    SetRegKey(RegKey,"AttrClear",AttrClear);

    SetRegKey(RegKey,"Flags",CurFilterData->Flags.Flags);
  }

  strcpy(RegKey,"Filters\\PanelMask");
  PtrRegKey=RegKey+strlen(RegKey);

  for (unsigned int i=0; i<TempFilterData.getCount(); i++)
  {
    CurFilterData = TempFilterData.getItem(i);
    itoa(i,PtrRegKey,10);

    const char *Mask;
    CurFilterData->GetMask(&Mask);
    SetRegKey(RegKey,"Mask",Mask);

    SetRegKey(RegKey,"Flags",CurFilterData->Flags.Flags);
  }

  SetRegKey("Filters","FoldersFilterFlags",FoldersFilter.Flags.Flags);
  SetRegKey("Filters","FolderFlags",FolderFlags.Flags);
}

void FileFilter::SwapPanelFlags(FileFilterParams *CurFilterData)
{
  DWORD flags=0;

  if (CurFilterData->Flags.Check(FFF_LPANELINCLUDE))
  {
    flags|=FFF_RPANELINCLUDE;
  }
  if (CurFilterData->Flags.Check(FFF_RPANELINCLUDE))
  {
    flags|=FFF_LPANELINCLUDE;
  }
  if (CurFilterData->Flags.Check(FFF_LPANELEXCLUDE))
  {
    flags|=FFF_RPANELEXCLUDE;
  }
  if (CurFilterData->Flags.Check(FFF_RPANELEXCLUDE))
  {
    flags|=FFF_LPANELEXCLUDE;
  }

  CurFilterData->Flags.Clear(FFF_RPANELEXCLUDE|FFF_LPANELEXCLUDE|FFF_RPANELINCLUDE|FFF_LPANELINCLUDE);
  CurFilterData->Flags.Set(flags);
}

void FileFilter::SwapFilter()
{
  for (unsigned int i=0; i<FilterData.getCount(); i++)
    SwapPanelFlags(FilterData.getItem(i));

  SwapPanelFlags(&FoldersFilter);

  for (unsigned int i=0; i<TempFilterData.getCount(); i++)
    SwapPanelFlags(TempFilterData.getItem(i));

  DWORD flags=0;
  if (FolderFlags.Check(FFF_LPANELINCLUDE))
  {
    flags|=FFF_RPANELINCLUDE;
  }
  if (FolderFlags.Check(FFF_RPANELINCLUDE))
  {
    flags|=FFF_LPANELINCLUDE;
  }
  FolderFlags.Clear(FFF_RPANELINCLUDE|FFF_LPANELINCLUDE);
  FolderFlags.Set(flags);
}

int FileFilter::ParseAndAddMasks(char **ExtPtr,const char *FileName,DWORD FileAttr,int& ExtCount,int Check)
{
  if (!strcmp(FileName,".") || TestParentFolderName(FileName) || (FileAttr & FA_DIREC))
    return -1;

  const char *DotPtr=strrchr(FileName,'.');
  char Mask[NM];
  /* $ 01.07.2001 IS
     ���� ����� �������� ����������� (',' ��� ';'), �� ������� �� �
     �������
  */
  if (DotPtr==NULL)
    strcpy(Mask,"*.");
  else if(strpbrk(DotPtr,",;"))
    sprintf(Mask,"\"*%s\"",DotPtr);
  else
    sprintf(Mask,"*%s",DotPtr);
  /* IS $ */

  // ������� �����...
  unsigned int Cnt=ExtCount;
  if(lfind((const void *)Mask,(void *)*ExtPtr,&Cnt,NM,ExtSort))
    return -1;

  // ... � ����� ��� ��������� ������!
  char *NewPtr;
  if ((NewPtr=(char *)xf_realloc(*ExtPtr,NM*(ExtCount+1))) == NULL)
    return 0;
  *ExtPtr=NewPtr;

  NewPtr=*ExtPtr+ExtCount*NM;
  xstrncpy(NewPtr,Mask,NM-2);

  NewPtr=NewPtr+strlen(NewPtr)+1;
  *NewPtr=Check;

  ExtCount++;

  return 1;
}

int _cdecl ExtSort(const void *el1,const void *el2)
{
  return LocalStricmp((char *)el1,(char *)el2);
}
