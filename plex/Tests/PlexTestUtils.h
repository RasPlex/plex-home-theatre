#ifndef PLEXTESTUTILS_H
#define PLEXTESTUTILS_H

#include "gtest/gtest.h"
#include "Client/PlexServer.h"

class PlexServerManagerTestUtility : public ::testing::Test
{
public:
  virtual void SetUp();
  virtual void TearDown();

  CPlexServerPtr server;
};

#endif // PLEXTESTUTILS_H
