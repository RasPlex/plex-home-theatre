#include "PlayQueueManager.h"

#include "Client/PlexServer.h"
#include "FileSystem/PlexFile.h"
#include "PlexJobs.h"
#include "PlexTypes.h"
#include "URL.h"

#include "JobManager.h"
#include "PlayList.h"
#include "PlayListPlayer.h"

#include "ApplicationMessenger.h"
#include "PlexApplication.h"
#include "Client/PlexServerManager.h"
#include "Application.h"

#include "dialogs/GUIDialogOK.h"
#include "settings/GUISettings.h"

using namespace PLAYLIST;

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlayQueueManager::CPlayQueueManager()
  : m_currentPlayQueueId(-1), m_currentPlayQueuePlaylist(PLAYLIST_NONE)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayQueueManager::loadMostRecentPlayQueue()
{
  if (g_guiSettings.GetString("system.mostrecentplayqueue").empty())
    return;

  CURL playQueueURL(g_guiSettings.GetString("system.mostrecentplayqueue"));

  CPlexServerPtr server = g_plexApplication.serverManager->FindByUUID(playQueueURL.GetHostName());
  if (!server)
  {
    CLog::Log(
          LOGDEBUG,
          "CPlayQueueManager::loadMostRecentPlayQueue wanted server %s, but it's not available.",
          playQueueURL.GetHostName().c_str());
    return;
  }

  CJobManager::GetInstance().AddJob(new CPlexPlayQueueFetchJob(playQueueURL, false), this);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayQueueManager::reconcilePlayQueueChanges(int playlistType, const CFileItemList& list)
{
  bool firstCommon = false;
  int listCursor = 0;
  int playlistCursor = 0;

  CPlayList playlist(g_playlistPlayer.GetPlaylist(playlistType));

  while (true)
  {
    CStdString playlistPath;
    if (playlistCursor < playlist.size())
      playlistPath = playlist[playlistCursor]->GetProperty("unprocessed_key").asString();

    if (listCursor >= list.Size())
      break;

    CStdString listPath = list.Get(listCursor)->GetProperty("unprocessed_key").asString();

    if (!firstCommon && !playlistPath.empty())
    {
      if (playlistPath == listPath)
      {
        firstCommon = true;
        listCursor++;
      }
      else
      {
        // remove the item at in the playlist.
        g_playlistPlayer.Remove(playlistType, playlistCursor);
        CLog::Log(LOGDEBUG,
                  "CPlayQueueManager::reconcilePlayQueueChanges removing item %d from playlist %d",
                  playlistCursor, playlistType);
      }
    }
    else if (playlistPath.empty())
    {
      CLog::Log(LOGDEBUG,
                "CPlayQueueManager::reconcilePlayQueueChanges adding item %s to playlist %d",
                listPath.c_str(), playlistType);
      g_playlistPlayer.Add(playlistType, list.Get(listCursor));
      listCursor++;
    }
    else if (playlistPath == listPath)
    {
      listCursor++;
    }
    else
    {
      g_playlistPlayer.Remove(playlistType, playlistCursor);
    }

    playlistCursor++;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayQueueManager::getPlayQueue(CPlexServerPtr server, int id, bool startPlaying)
{
  CStdString path;
  path.Format("/playQueues/%d", id);

  CURL u = server->BuildPlexURL(path);
  CJobManager::GetInstance().AddJob(new CPlexPlayQueueFetchJob(u, startPlaying), this);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CStdString CPlayQueueManager::getURIFromItem(const CFileItem& item, const CStdString& uri)
{
  if (item.GetPath().empty())
    return "";

  CURL u(item.GetPath());

  if (u.GetProtocol() != "plexserver")
    return "";

  CStdString itemDirStr = "item";
  if (item.m_bIsFolder)
    itemDirStr = "directory";

  if (!item.HasProperty("librarySectionUUID"))
  {
    CLog::Log(LOGWARNING, "CPlayQueueManager::getURIFromItem item %s doesn't have a section UUID",
              item.GetPath().c_str());
    return "";
  }

  CStdString realURI =
  uri.empty() ? (CStdString)item.GetProperty("unprocessed_key").asString() : uri;
  CURL::Encode(realURI);

  CStdString ret;
  ret.Format("library://%s/%s/%s", item.GetProperty("librarySectionUUID").asString(), itemDirStr,
             realURI);

  return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlayQueueManager::createPlayQueueFromItem(const CPlexServerPtr& server,
                                                const CFileItemPtr& item, bool shuffle,
                                                bool continuous, int limit)
{
  if (!server)
    return false;

  ePlexMediaType type = PlexUtils::GetMediaTypeFromItem(item);
  if (type == PLEX_MEDIA_TYPE_UNKNOWN)
    return false;

  std::string containerPath = getURIFromItem(*item.get());
  std::string key;

  if (!item->m_bIsFolder)
    key = item->GetPath();

  return createPlayQueue(server, type, containerPath, key, shuffle, continuous, limit);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CURL CPlayQueueManager::getPlayQueueURL(const CPlexServerPtr& server, ePlexMediaType type,
                                        const std::string& uri, const std::string& key,
                                        bool shuffle, bool continuous, int limit, bool next)
{
  if (!server)
    return CURL();

  CURL u = server->BuildPlexURL("/playQueues");
  std::string typeStr = PlexUtils::GetMediaTypeString(type);

  if (typeStr == "music")
  {
    // here we expect audio for some reason
    typeStr = "audio";
  }

  u.SetOption("type", typeStr);
  u.SetOption("uri", uri);

  if (!key.empty())
    u.SetOption("key", key);
  if (shuffle)
    u.SetOption("shuffle", "1");
  if (continuous)
    u.SetOption("continuous", "1");
  if (limit > 0)
    u.SetOption("limit", boost::lexical_cast<std::string>(limit));
  if (next)
    u.SetOption("next", "1");

  return u;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlayQueueManager::addItemToCurrentPlayQueue(const CFileItemPtr& item, bool playNext)
{
  if (!verifyServerInItem(item))
    return false;

  CStdString uri = getURIFromItem(*item);
  CPlexServerPtr server = g_plexApplication.serverManager->FindFromItem(item);

  if (server)
  {
    CURL u = getPlayQueueURL(server, PlexUtils::GetMediaTypeFromItem(item), uri, "", false, false,
                             0, playNext);

    if (u.Get().empty())
      return false;

    CPlexPlayQueueFetchJob* job = new CPlexPlayQueueFetchJob(u, true);
    job->m_dir.SetHTTPVerb("PUT");
    CJobManager::GetInstance().AddJob(job, this);
    return true;
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlayQueueManager::createPlayQueue(const CPlexServerPtr& server, ePlexMediaType type,
                                        const std::string& uri, const std::string& key,
                                        bool shuffle, bool continuous, int limit)
{
  CURL u = getPlayQueueURL(server, type, uri, key, shuffle, continuous, limit);

  if (u.Get().empty())
    return false;

  CPlexPlayQueueFetchJob* job = new CPlexPlayQueueFetchJob(u, true);
  job->m_dir.SetHTTPVerb("POST");
  CJobManager::GetInstance().AddJob(job, this);
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
int CPlayQueueManager::getPlaylistFromString(const CStdString& typeStr)
{
  if (typeStr.Equals("audio"))
    return PLAYLIST_MUSIC;
  else if (typeStr.Equals("video"))
    return PLAYLIST_VIDEO;
  else if (typeStr.Equals("photo"))
    return PLAYLIST_PICTURE;
  return PLAYLIST_NONE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
int CPlayQueueManager::getPlaylistFromType(ePlexMediaType type)
{
  switch (type)
  {
    case PLEX_MEDIA_TYPE_MUSIC:
      return PLAYLIST_MUSIC;
    case PLEX_MEDIA_TYPE_VIDEO:
      return PLAYLIST_VIDEO;
    default:
      return PLAYLIST_NONE;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayQueueManager::refreshPlayQueue(const CFileItemPtr& item)
{
  if (!item)
    return;

  CPlexServerPtr server = g_plexApplication.serverManager->FindFromItem(item);
  if (server)
    getPlayQueue(server, item->GetProperty("playQueueID").asInteger(), false);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
void CPlayQueueManager::OnJobComplete(unsigned int jobID, bool success, CJob* job)
{
  CPlexPlayQueueFetchJob* fj = static_cast<CPlexPlayQueueFetchJob*>(job);
  if (fj && success)
  {
    int playlist = getPlaylistFromType(PlexUtils::GetMediaTypeFromItem(fj->m_items));
    int selectedOffset = fj->m_items.GetProperty("playQueueSelectedItemOffset").asInteger();
    int playQueueID = fj->m_items.GetProperty("playQueueID").asInteger();

    if (playlist == m_currentPlayQueuePlaylist && playQueueID == m_currentPlayQueueId)
    {
      CLog::Log(LOGDEBUG, "CPlayQueueManager::OnJobComplete refreshing current playlist %d",
                playQueueID);
      reconcilePlayQueueChanges(playlist, fj->m_items);

      // now we need to check if the current playing item is still in the list, otherwise we'll
      // advance the playlist.
      if (g_playlistPlayer.GetCurrentSong() == -1)
        CApplicationMessenger::Get().PlayListPlayerNext();

      return;
    }

    if (playlist == PLAYLIST_NONE)
    {
      CGUIDialogOK::ShowAndGetInput("Error creating the Play Queue",
                                    "The reponse from the server did not make sense.", "", "");
      return;
    }

    if (fj->m_items.Size() == 0)
    {
      CGUIDialogOK::ShowAndGetInput("Error creating the Play Queue",
                                    "The server responded with a empty Play Queue", "", "");
      return;
    }

    CLog::Log(LOGDEBUG,
              "CPlayQueueManager::OnJobComplete going to play a playlist of size %d and type %d",
              fj->m_items.Size(), playlist);

    g_playlistPlayer.SetCurrentPlaylist(playlist);
    CApplicationMessenger::Get().PlayListPlayerClear(playlist);
    CApplicationMessenger::Get().PlayListPlayerAdd(playlist, fj->m_items);
    g_playlistPlayer.SetCurrentSong(selectedOffset);

    if (fj->m_startPlaying)
      CApplicationMessenger::Get().PlayListPlayerPlay();

    m_currentPlayQueuePlaylist = playlist;
    m_currentPlayQueueId = playQueueID;

    CPlexServerPtr server = g_plexApplication.serverManager->FindFromItem(fj->m_items);
    if (server)
    {
      CStdString path;
      path.Format("/playQueues/%d", m_currentPlayQueueId);
      g_guiSettings.SetString("system.mostrecentplayqueue", server->BuildPlexURL(path).Get());
      m_currentPlayQueueServer = server;
    }

  }
  else
  {
    CGUIDialogOK::ShowAndGetInput("Error creating the Play Queue",
                                  "The server was unable to create the Play Queue", "", "");
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlayQueueManager::verifyServerInItem(const CFileItemPtr& item)
{
  if (!item)
    return false;

  CPlexServerPtr server = g_plexApplication.serverManager->FindFromItem(item);
  if (!server)
    return false;

  return server->Equals(m_currentPlayQueueServer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlayQueueManager::removeItemFromCurrentPlayQueue(const CFileItemPtr& item)
{
  if (!verifyServerInItem(item))
    return false;

  if (!item || !item->HasProperty("playQueueItemID"))
    return false;

  CStdString path;
  path.Format("/playQueues/%d/items/%d", m_currentPlayQueueId,
              item->GetProperty("playQueueItemID").asInteger());
  CURL u = m_currentPlayQueueServer->BuildPlexURL(path);

  CPlexDirectoryFetchJob* job = new CPlexDirectoryFetchJob(u);
  job->m_dir.SetHTTPVerb("DELETE");
  CJobManager::GetInstance().AddJob(job, this);
  return true;
}
