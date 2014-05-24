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
#include "xbmc/Util.h"

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
CStdString CPlexUpdaterJob::StreamExec(CStdString command)
{
  CStdString output = "";
  command += "2>&1";
  CLog::Log(LOGDEBUG,"CPlexUpdaterJob::StreamExec : Executing '%s'", command.c_str());
  FILE* fp = popen(command.c_str(), "r");
  if (fp)
  {
    // we grab script output in case we would have an error
    char buffer[128];
    CStdString commandOutput; 

    while(!feof(fp)){
      if ( fgets(buffer, sizeof(buffer), fp)!=NULL )
      {
        output += buffer;
        commandOutput = CStdString(buffer);
        CLog::Log(LOGINFO, "CPlexUpdaterJob::StreamExec: %s",commandOutput.c_str());
      }
    }

    int retcode = pclose(fp);
    if (retcode)
    {
      CLog::Log(LOGERROR,"CPlexUpdaterJob::StreamExec: error %d while running install", retcode);
    }
  }
  return output;
}


/* OPENELEC */
bool CPlexUpdaterJob::DoWork()
{

  CStdString command, output;
  CStdString contents;
  CStdString kernel, system, kernel_md5, system_md5, post_update;
  int steps = 8; // hardcoded based on count below
  int step  = 1;
  m_dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);

  if (m_dlgProgress)
  {
    m_dlgProgress->SetHeading("Updating");
    m_dlgProgress->StartModal();
    m_dlgProgress->ShowProgressBar(true);

  }

  // run the script redirecting stderr to stdin so that we can grab script errors and log them
  //CStdString command = "/bin/sh " + updaterPath + " " + CSpecialProtocol::TranslatePath(m_localBinary) + " ";

  // Do the actual update here, translate this to C++:

  SetProgress("Preparing directory", step++, steps); // 1

  command = "mkdir -p /storage/.update/tmp";
  StreamExec(command);


  SetProgress("Extracting update...", step++, steps); // 2

  command = "tar -xf "+CSpecialProtocol::TranslatePath(m_localBinary)+" -C /storage/.update/tmp";
  output = StreamExec(command);

  SetProgress("Examining archive...", step++, steps); // 3

  command = "find /storage/.update/tmp/";
  contents = StreamExec(command);

  SetProgress("Checking archive integrity...", step++, steps); // 4

  command = "echo "+contents+" | tr \" \" \"\n\" | KERNEL$";
  kernel  = StreamExec(command);
  CLog::Log(LOGDEBUG, "CPlexUpdaterJob::DoWork: kernel: %s", kernel);

  command = "echo "+contents+" | tr \" \" \"\n\" | SYSTEM$";
  system  = StreamExec(command);
  CLog::Log(LOGDEBUG, "CPlexUpdaterJob::DoWork: system: %s", system);

  command = "echo "+contents+" | tr \" \" \"\n\" | KERNEL.md5";
  kernel_md5  = StreamExec(command);
  CLog::Log(LOGDEBUG, "CPlexUpdaterJob::DoWork: kernel check: %s", kernel_md5);

  command = "echo "+contents+" | tr \" \" \"\n\" | SYSTEM.md5";
  system_md5  = StreamExec(command);
  CLog::Log(LOGDEBUG, "CPlexUpdaterJob::DoWork: system check: %s", system_md5);

  command = "echo "+contents+" | tr \" \" \"\n\" | post_update.sh";
  post_update = StreamExec(command); 
  CLog::Log(LOGDEBUG, "CPlexUpdaterJob::DoWork: post_update: %s", post_update);

  SetProgress("Examining archive...", step++, steps); // 5

  // check if any of the above were empty
  
  SetProgress("Validating checksums", step++, steps); // 6

  // compute and compare checksums 

/*
kernel_check=`/bin/md5sum $KERNEL | awk '{print $1}'`
system_check=`/bin/md5sum $SYSTEM | awk '{print $1}'`

kernelmd5=`cat $KERNELMD5 | awk '{print $1}'`
systemmd5=`cat $SYSTEMMD5 | awk '{print $1}'`

[ "$kernel_check" != "$kernelmd5" ] && abort 'Kernel checksum mismatch'
[ "$system_check" != "$systemmd5" ] && abort 'System checksum mismatch'


*/
  SetProgress("Preparing update files", step++, steps); // 7

  // move the files to the update directory

/*
# move extracted files to the toplevel
mv $KERNEL $SYSTEM $KERNELMD5 $SYSTEMMD5 .

*/

  SetProgress("Running post-update steps", step++, steps); // 8

/*

POST_UPDATE_PATH=/storage/.post_update.sh
if [ -n $POST_UPDATE ] && [-f "$POST_UPDATE" ];then
  notify 'Running post update script'
  cp $POST_UPDATE $POST_UPDATE_PATH
  post_update
  notify 'Post-update complete!'
fi

*/

  SetProgress("Cleaning up", step++, steps); // 9

  // remove temporary folders
/*
# remove the directories created by tar
rm -r /storage/.update/tmp
rm $UPDATEFILE
*/

  m_autoupdater -> WriteUpdateInfo();
  m_dlgProgress->Close();

  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Update is complete!", "System will reboot twice to apply.", 10000, false);
  StreamExec("/sbin/reboot")

}


void CPlexUpdaterJob::SetProgress(char* message, int step, int steps)
{
  CStdString progressMsg;
  CStdString update;
  float percentage = ((step * 100 / steps)/100);

  update.Format( " %d / %d : '%s'", step, steps, message );

  m_dlgProgress->SetLine(0, update);

  if (percentage > 0)
    progressMsg.Format( "Progress : %2d%%", percentage);
  else
    progressMsg = "";

  m_dlgProgress->SetLine(2, progressMsg);
  m_dlgProgress->SetPercentage(percentage);
}
#endif

bool CPlexRecursiveFetchJob::DoWork()
{
  CUtil::GetRecursiveListing(m_url, *m_list, m_exts);
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CPlexTextureCacheJob::CacheTexture(CBaseTexture **texture)
{
  // unwrap the URL as required
  std::string additional_info;
  unsigned int width, height;
  CStdString image = DecodeImageURL(m_url, width, height, additional_info);

  // generate the hash
  m_details.hash = GetImageHash(image);
  if (m_details.hash.empty())
    return false;
  else if (m_details.hash == m_oldHash)
    return true;

  int bytesRead, bufferSize = 131072;
  unsigned char buffer[131072];
  bool outputFileOpenned = false;

  if (m_inputFile.Open(image, READ_NO_CACHE))
  {
    while ((bytesRead = m_inputFile.Read(buffer, bufferSize)))
    {
      // eventually open output file depending upon filetype
      if (!outputFileOpenned)
      {
        // we need to check if its a jpg or png
        if ((buffer[0] == 0xFF) && (buffer[1] == 0xD8))
          m_details.file = m_cachePath + ".jpg";
        else
          m_details.file = m_cachePath + ".png";

        // now open the file
        if (m_outputFile.OpenForWrite(CTextureCache::GetCachedPath(m_details.file), true))
        {
          outputFileOpenned = true;
        }
        else
        {
          m_inputFile.Close();
          CLog::Log(LOGERROR,"CTextureCacheJob::CacheTexture unable to open output file %s",CTextureCache::GetCachedPath(m_details.file).c_str());
          return false;
        }
      }

      m_outputFile.Write(buffer,bytesRead);
    }

    m_outputFile.Flush();
    m_inputFile.Close();
    m_outputFile.Close();
    return true;
  }
  else
  {
    CLog::Log(LOGERROR,"CTextureCacheJob::CacheTexture unable to open input file %s",image.c_str());
    return false;
  }
}
