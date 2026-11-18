#ifndef STARFIELDSAVEGAME_H
#define STARFIELDSAVEGAME_H

#include "gamebryosavegame.h"

#include <Windows.h>

class GameStarfield;

class StarfieldSaveGame : public GamebryoSaveGame
{
public:
  StarfieldSaveGame(QString const& fileName, GameStarfield const* game);

protected:
  std::unique_ptr<DataFields> fetchDataFields() const override;

private:
  QStringList m_Plugins;
  QStringList m_LightPlugins;
  QStringList m_MediumPlugins;
};

#endif  // STARFIELDSAVEGAME_H
