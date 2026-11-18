#ifndef _STARFIELDGAMEPLUGINS_H
#define _STARFIELDGAMEPLUGINS_H

#include <creationgameplugins.h>

#include <QObject>
#include <QtGlobal>

class StarfieldGamePlugins : public CreationGamePlugins
{

public:
  StarfieldGamePlugins(MOBase::IOrganizer* organizer);

protected:
  virtual bool mediumPluginsAreSupported() override;
  virtual void writePluginList(const MOBase::IPluginList* pluginList,
                               const QString& filePath) override;
  virtual QStringList readPluginList(MOBase::IPluginList* pluginList) override;

private:
  // True when plugins.txt in the profile was last written by something other than
  // MO2 -- in practice Starfield itself, which rewrites the file when a save is
  // loaded or the Creations menu is opened and leaves core plugins unticked.
  bool pluginsTxtRewrittenExternally() const;

  // Ticks plugins that ship in the game's Data folder and BlueprintShips
  // sidecars whose corresponding main plugin is active.
  void reactivateVanillaPlugins(MOBase::IPluginList* pluginList) const;
};

#endif  // _STARFIELDGAMEPLUGINS_H
