
#ifndef _PLEXGLOBALCACHER_H_
#define _PLEXGLOBALCACHER_H_

#include "VideoThumbLoader.h"
#include "threads/Thread.h"
#include "threads/Event.h"
#include "dialogs/GUIDialogProgress.h"
#include "threads/CriticalSection.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
class CPlexGlobalCacher : public CThread
{
public :
  static CPlexGlobalCacher* GetInstance();
  static void DeleteInstance();
  void Start();
  void Process();
  void OnExit();
  void Continue(bool cont);
  CFileItemPtr PickItem();

private:
  CPlexGlobalCacher();
  static CPlexGlobalCacher* m_globalCacher;
  bool m_continue;
  CFileItemList m_listToCache;
  CGUIDialogProgress *m_dlgProgress;
  CCriticalSection m_picklock;

};

///////////////////////////////////////////////////////////////////////////////////////////////////
class CPlexGlobalCacherWorker : public CThread
{
private:
  CPlexGlobalCacher *m_pCacher;

public:
  CPlexGlobalCacherWorker(CPlexGlobalCacher *pCacher) : CThread("CPlexGlobalCacherWorker"), m_pCacher(pCacher) {}
  void Process();

};


void controlGlobalCache();
#endif /* _PLEXGLOBALCACHER_H_*/
