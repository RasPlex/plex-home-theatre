#include "Utility/PlexGlobalCacher.h"
#include "FileSystem/PlexDirectory.h"
#include "Client/PlexServerDataLoader.h"
#include "PlexApplication.h"
#include "utils/Stopwatch.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "guilib/GUITextBox.h"
#include "guilib/GUIEditControl.h"
#include "dialogs/GUIDialogYesNo.h"
#include "dialogs/GUIDialogSelect.h"
#include "utils/log.h"
#include "filesystem/File.h"
#include "TextureCache.h"
#include "guilib/GUIWindowManager.h"
#include <algorithm>
#include "LocalizeStrings.h"

using namespace XFILE;

CPlexGlobalCacher* CPlexGlobalCacher::m_globalCacher = NULL;

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexGlobalCacher::CPlexGlobalCacher() : CThread("Plex Global Cacher")
{
  m_continue = true;

  m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
  if (m_dlgProgress)
  {
    m_dlgProgress->SetHeading(2);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexGlobalCacher* CPlexGlobalCacher::GetInstance()
{
  if (!m_globalCacher)   // Only allow one instance of class to be generated.
    m_globalCacher = new CPlexGlobalCacher();
  return m_globalCacher;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexGlobalCacher::DeleteInstance()
{
  if (m_globalCacher)
    delete m_globalCacher;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexGlobalCacher::Continue(bool cont)
{
  m_continue = cont;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexGlobalCacher::Start()
{
  CLog::Log(LOGNOTICE,"Global Cache : Creating cacher thread");
  CThread::Create(true);
  CLog::Log(LOGNOTICE,"Global Cache : Cacher thread created");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void controlGlobalCache()
{
  CPlexGlobalCacher* cacher = CPlexGlobalCacher::GetInstance();

  if (!cacher->IsRunning())
  {
    cacher->Continue(true);
    cacher->Start();
  }
  else if (cacher ->IsRunning())
  {
    bool ok = CGUIDialogYesNo::ShowAndGetInput("Stop global caching?",
                                               "A reboot is recommended after stopping ",
                                               "the global caching process.",
                                               "Stopping may not occur right away.",
                                               "No!", "Yes");

    if (ok)
    {
      CLog::Log(LOGNOTICE,"Global Cache : Cacher thread stopped by user");
      cacher -> Continue(false);
      cacher -> StopThread(false);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexGlobalCacher::Process()
{
  CPlexDirectory dir;
  CFileItemList list;
  CStopWatch timer;
  CStopWatch looptimer;
  CStopWatch msgtimer;
  CStdString message;
  CStdString heading;
  int totalsize =0;

  // list the arts we want to cache
  CStdStringArray art;
  art.push_back("smallThumb");
  art.push_back("thumb");
  art.push_back("bigthumb"); //*
  art.push_back("smallPoster");
  art.push_back("poster");
  art.push_back("bigPoster");//*
  art.push_back("smallGrandparentThumb");
  art.push_back("grandparentThumb");
  art.push_back("bigGrandparentThumb");
  art.push_back("fanart");
  art.push_back("banner");

  CGUIDialogSelect *dialog = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
  if (!dialog)
  {
    CLog::Log(LOGERROR,"CPlexGlobalCacher::Process Cannot find WINDOW_DIALOG_SELECT");
    return ;
  }

  // grab all the sections
  CFileItemListPtr pAllSharedSections = g_plexApplication.dataLoader->GetAllSharedSections();

  CLog::Log(LOGNOTICE,"Global Cache : found %d Shared Sections",pAllSharedSections->Size());

  // get all the sections names
  CFileItemListPtr pAllSections = g_plexApplication.dataLoader->GetAllSections();
  pAllSections->Append(*pAllSharedSections);

  CLog::Log(LOGNOTICE,"Global Cache : found %d Regular Sections",pAllSections->Size());

  CFileItemList items;
  std::set<CStdString> servers;

  // build the servers list
  for (int iSection = 0; iSection < pAllSections->Size() && m_continue; iSection ++)
  {
    CFileItemPtr section = pAllSections->Get(iSection);
    CStdString servername = section->GetProperty("serverName").asString();

    if (servers.find(servername) == servers.end())
    {
      servers.insert(servername);
      CFileItemPtr item(new CFileItem("", false));
      item->SetLabel( servername );
      item->SetProperty("serverName",servername);
      items.Add(item);
    }
  }

  // Display server list selection dialog
  heading = g_localizeStrings.Get(41002);
  dialog->Reset();
  dialog->SetHeading(heading);
  dialog->SetItems(&items);
  dialog->SetMultiSelection(true);
  dialog->EnableButton(true, 209);
  dialog->SetUseDetails(true);
  dialog->DoModal();
  CFileItemList* selectedSections = new CFileItemList();

  if (dialog->IsButtonPressed())
  {
    // switch to the addons browser.
    const CFileItemList& selectedItems = dialog->GetSelectedItems();
    std::vector<CStdString> selected;

    CLog::Log(LOGNOTICE,"%i servers selected",selectedItems.Size());

    // build the selected servers list
    for (int iSection = 0; iSection < selectedItems.Size(); iSection ++)
    {
      CFileItemPtr section = selectedItems.Get(iSection);
      CStdString servername = section->GetProperty("serverName").asString();
      CLog::Log(LOGNOTICE,"Server %s selected",servername.c_str());
      selected.push_back(servername);
    }

    // build the selected sections from selected servers
    for (int iSection = 0; iSection < pAllSections->Size() && m_continue; iSection ++)
    {
      CFileItemPtr section = pAllSections->Get(iSection);
      CStdString servername = section->GetProperty("serverName").asString();
      
      if (std::find(selected.begin(), selected.end(), servername) != selected.end())
      {
        CLog::Log(LOGNOTICE,"Server %s was selected so adding its section %s",servername.c_str(), section->GetLabel().c_str());
        selectedSections->Add(section);
      }
      else
        CLog::Log(LOGNOTICE,"Section %s removed as server %s wasn't selected",section->GetLabel().c_str(),servername.c_str());
    }

    // setup the progress dialog info
    m_dlgProgress->SetHeading(g_localizeStrings.Get(41004));
    m_dlgProgress->StartModal();
    m_dlgProgress->ShowProgressBar(true);
    timer.StartZero();
    msgtimer.StartZero();

    // now just process the items
    for (int iSection = 0; iSection < selectedSections->Size() && m_continue; iSection ++)
    {

      m_continue = !m_dlgProgress->IsCanceled();
      if(!m_continue)
        break;

      list.Clear();

      looptimer.StartZero();
      CFileItemPtr Section = selectedSections->Get(iSection);
      message.Format( g_localizeStrings.Get(41003) + " %d / %d : '%s'",iSection+1,selectedSections->Size(),Section->GetLabel());
      m_dlgProgress->SetLine(0,message);

      // Pop the notification
      message.Format(g_localizeStrings.Get(41005) + " '%s'...", Section->GetLabel());
      m_dlgProgress->SetLine(1,message);
      m_dlgProgress->SetLine(2,"");
      m_dlgProgress->SetPercentage(0);

      // gets all the data from one section
      CURL url(Section->GetPath());
      PlexUtils::AppendPathToURL(url, "all");

      // store them into the list
      dir.GetDirectory(url, list);

      // Grab the server Name
      CStdString ServerName = "<unknown>";
      if (list.Size())
      {
        CPlexServerPtr  pServer = g_plexApplication.serverManager->FindFromItem(list.Get(0));
        if (pServer)
        {
          ServerName = pServer->GetName();
        }

        CLog::Log(LOGNOTICE,"Global Cache : Processed +%d items in '%s' on %s (total %d), took %f (total %f)", list.Size(), Section->GetLabel().c_str(), ServerName.c_str(), totalsize, looptimer.GetElapsedSeconds(),timer.GetElapsedSeconds());
      }

      totalsize += list.Size();

      // Here we have the file list, just process the items
      for (int i = 0; i < list.Size() && m_continue; i++)
      {
        // check for user cancel
        m_continue = !m_dlgProgress->IsCanceled();
        if(!m_continue)
          break;

        CFileItemPtr item = list.Get(i);
        if (item->IsPlexMediaServer())
        {
          // display some progress
          message.Format(g_localizeStrings.Get(41003) + " %d / %d : '%s' on '%s' ", iSection+1, selectedSections->Size(), Section->GetLabel(), ServerName);
          m_dlgProgress->SetLine(0,message);

          m_dlgProgress->SetPercentage(i*100 / list.Size());

          message.Format(g_localizeStrings.Get(41006) + " %d/%d ...", i, list.Size());
          m_dlgProgress->SetLine(1,message);

          message.Format(g_localizeStrings.Get(41007) + " : %2d%%",i*100 / list.Size());
          m_dlgProgress->SetLine(2,message);

          BOOST_FOREACH(CStdString artKey, art)
          {
            if (item->HasArt(artKey) &&
                !CTextureCache::Get().HasCachedImage(item->GetArt(artKey)))
              CTextureCache::Get().CacheImage(item->GetArt(artKey));
          }

          // check if cancel button has been pressed
          if (m_dlgProgress->IsCanceled())
          {
            CLog::Log(LOGNOTICE,"Global Cache : Operation was canceled by user");
            break;
          }
        }

        m_dlgProgress->Progress();
      }
      CLog::Log(LOGNOTICE,"Global Cache : Processing section %s took %f",Section->GetLabel().c_str(), timer.GetElapsedSeconds());
    }

    CLog::Log(LOGNOTICE,"Global Cache : Full operation took %f",timer.GetElapsedSeconds());
  }

  if (!dialog->IsConfirmed())
    return ;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexGlobalCacher::OnExit()
{
  m_dlgProgress->Close();
  m_globalCacher = NULL;
}
