//
//  PlexJobs.cpp
//  Plex Home Theater
//
//  Created by Tobias Hieta on 2013-08-14.
//
//

#include "PlexJobs.h"
#include "FileSystem/PlexDirectory.h"

#include "FileSystem/PlexFile.h"

#include "TextureCache.h"
#include "File.h"
#include "utils/Crc32.h"
#include "PlexFile.h"
#include "video/VideoInfoTag.h"
#include "Stopwatch.h"
#include "PlexUtils.h"

////////////////////////////////////////////////////////////////////////////////
bool CPlexHTTPFetchJob::DoWork()
{
  return m_http.Get(m_url.Get(), m_data);
}

////////////////////////////////////////////////////////////////////////////////
bool CPlexHTTPFetchJob::operator==(const CJob* job) const
{
  const CPlexHTTPFetchJob *f = static_cast<const CPlexHTTPFetchJob*>(job);
  return m_url.Get() == f->m_url.Get();
}

////////////////////////////////////////////////////////////////////////////////
bool CPlexDirectoryFetchJob::DoWork()
{
  return m_dir.GetDirectory(m_url.Get(), m_items);
}

////////////////////////////////////////////////////////////////////////////////
bool CPlexMediaServerClientJob::DoWork()
{
  bool success = false;
  
  if (m_verb == "PUT")
    success = m_http.Put(m_url.Get(), m_data);
  else if (m_verb == "GET")
    success = m_http.Get(m_url.Get(), m_data);
  else if (m_verb == "DELETE")
    success = m_http.Delete(m_url.Get(), m_data);
  else if (m_verb == "POST")
    success = m_http.Post(m_url.Get(), m_postData, m_data);
  
  return success;
}

////////////////////////////////////////////////////////////////////////////////////////
bool CPlexVideoThumbLoaderJob::DoWork()
{
  if (!m_item->IsPlexMediaServer())
    return false;

  CStdStringArray art;
  art.push_back("smallThumb");
  art.push_back("smallPoster");
  art.push_back("smallGrandparentThumb");
  art.push_back("banner");

  int i = 0;
  BOOST_FOREACH(CStdString artKey, art)
  {
    if (m_item->HasArt(artKey) &&
        !CTextureCache::Get().HasCachedImage(m_item->GetArt(artKey)))
      CTextureCache::Get().BackgroundCacheImage(m_item->GetArt(artKey));

    if (ShouldCancel(i++, art.size()))
      return false;
  }

  return true;
}

using namespace XFILE;

////////////////////////////////////////////////////////////////////////////////////////
bool
CPlexDownloadFileJob::DoWork()
{
  CFile file;
  CURL theUrl(m_url);
  m_http.SetRequestHeader("X-Plex-Client", PLEX_TARGET_NAME);

  if (!file.OpenForWrite(m_destination, true))
  {
    CLog::Log(LOGWARNING, "[DownloadJob] Couldn't open file %s for writing", m_destination.c_str());
    return false;
  }

  if (m_http.Open(theUrl))
  {
    CLog::Log(LOGINFO, "[DownloadJob] Downloading %s to %s", m_url.c_str(), m_destination.c_str());

    bool done = false;
    bool failed = false;
    int64_t read;
    int64_t leftToDownload = m_http.GetLength();
    int64_t total = leftToDownload;

    while (!done)
    {
      char buffer[4096];
      read = m_http.Read(buffer, 4096);
      if (read > 0)
      {
        leftToDownload -= read;
        file.Write(buffer, read);
        done = ShouldCancel(total-leftToDownload, total);
        if(done) failed = true;
      }
      else if (read == 0)
      {
        done = true;
        failed = total == 0;
        continue;
      }

      if (total == 0)
        done = true;
    }

    CLog::Log(LOGINFO, "[DownloadJob] Done with the download.");

    m_http.Close();
    file.Close();

    return !failed;
  }

  CLog::Log(LOGWARNING, "[DownloadJob] Failed to download file.");
  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlexThemeMusicPlayerJob::DoWork()
{
  if (m_themeUrl.empty())
    return false;

  Crc32 crc;
  crc.ComputeFromLowerCase(m_themeUrl);

  CStdString hex;
  hex.Format("%08x", (unsigned int)crc);

  m_fileToPlay = "special://masterprofile/ThemeMusicCache/" + hex + ".mp3";

  if (!XFILE::CFile::Exists(m_fileToPlay))
  {
    CPlexFile plex;
    CFile localFile;

    if (!localFile.OpenForWrite(m_fileToPlay, true))
    {
      CLog::Log(LOGWARNING, "CPlexThemeMusicPlayerJob::DoWork failed to open %s for writing.", m_fileToPlay.c_str());
      return false;
    }

    bool failed = false;

    if (plex.Open(m_themeUrl))
    {
      bool done = false;
      int64_t read = 0;

      while(!done)
      {
        char buffer[4096];
        read = plex.Read(buffer, 4096);
        if (read > 0)
        {
          localFile.Write(buffer, read);
          done = ShouldCancel(0, 0);
          if (done) failed = true;
        }
        else if (read == 0)
        {
          done = true;
          continue;
        }
      }
    }

    CLog::Log(LOGDEBUG, "CPlexThemeMusicPlayerJob::DoWork cached %s => %s", m_themeUrl.c_str(), m_fileToPlay.c_str());

    plex.Close();
    localFile.Close();

    return !failed;
  }
  else
    return true;
}


#ifdef TARGET_RASPBERRY_PI


/* OPENELEC */
bool CPlexUpdaterJob::DoWork()
{
  // we need to start the Install script here

  // build script path
  CStdString updaterPath;
  CUtil::GetHomePath(updaterPath);
  updaterPath += "/tools/openelec_install_update.sh";

  // run the script redirecting stderr to stdin so that we can grab script errors and log them
  CStdString command = "/bin/sh " + updaterPath + " " + CSpecialProtocol::TranslatePath(m_localBinary) + " 2>&1";
  CLog::Log(LOGDEBUG,"CPlexAutoUpdate::UpdateAndRestart : Executing '%s'", command.c_str());
  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Launching updater", "Progress will be reported. Please be patient", 10000, false);

  //http://www.sw-at.com/blog/2011/03/23/popen-execute-shell-command-from-cc/ should replace with streaming execution, so we can see the output of the script in the log
  FILE* fp = popen(command.c_str(), "r");
  if (fp)
  {
    // we grab script output in case we would have an error
    char output[1000];
    CStdString commandOutput;

    while(fgets(output, sizeof(output), fp)!=NULL){
      commandOutput = CStdString(output);
      CLog::Log(LOGINFO, "CPlexAutoUpdate::UpdateAndRestart: %s",commandOutput.c_str());
    }

    int retcode = fclose(fp);
    if (retcode)
    {
      CLog::Log(LOGERROR,"CPlexAutoUpdate::UpdateAndRestart: error %d while running install", retcode);
      return false;
    }
  }

  m_autoupdater -> WriteUpdateInfo();
  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Update is complete!", "System will reboot twice to apply.", 10000, false);
  fp = popen("/sbin/reboot", "r");
  if (fp)
  {
    // we grab script output in case we would have an error
    char output[1000];
    CStdString commandOutput;
    if (fgets(output, sizeof(output)-1, fp))
      commandOutput = CStdString(output);

    int retcode = fclose(fp);
    if (retcode)
    {
      CLog::Log(LOGERROR,"CPlexAutoUpdate::UpdateAndRestart: error %d! Couldn't restart", retcode );
      return false;
    }
  }

  return true;
}
#endif
