
#ifndef _PLEXGLOBALCACHER_H_
#define _PLEXGLOBALCACHER_H_

#include "VideoThumbLoader.h"
#include "threads/Thread.h"
#include "threads/Event.h"
#include "dialogs/GUIDialogProgress.h"

class CPlexGlobalCacher : public CThread
{
public :
  static CPlexGlobalCacher* GetInstance();
  static void DeleteInstance();
  void Start();
  void Process();
  void OnExit();
  void Continue(bool cont);

private:
  CPlexGlobalCacher();
  static CPlexGlobalCacher* m_globalCacher;
  bool m_continue;
  CGUIDialogProgress *m_dlgProgress;

};

void controlGlobalCache();
#endif /* _PLEXGLOBALCACHER_H_*/ 
