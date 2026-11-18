#include "starfieldsavegame.h"

#include <Windows.h>

#include "gamestarfield.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <zlib.h>

namespace
{

constexpr quint64 MaxUncompressedSaveSize = 512ULL * 1024 * 1024;
constexpr quint64 BcpsHeaderSize         = 0x48;

class NeedMoreData final : public std::exception
{};

quint64 readLittleEndian(QByteArray const& data, quint64 offset, unsigned width)
{
  if (offset > static_cast<quint64>(data.size()) ||
      width > static_cast<quint64>(data.size()) - offset) {
    throw std::runtime_error("unexpected end of Starfield save");
  }

  quint64 value = 0;
  for (unsigned i = 0; i < width; ++i) {
    value |= static_cast<quint64>(
                 static_cast<unsigned char>(data.at(static_cast<qsizetype>(offset + i))))
             << (i * 8);
  }
  return value;
}

class SaveReader
{
public:
  SaveReader(
      QByteArray const& data, qsizetype start = 0, qsizetype end = -1,
      quint64 totalSize = std::numeric_limits<quint64>::max(),
      bool allowMoreData = false)
      : m_Data(data), m_Position(start), m_End(end < 0 ? data.size() : end),
        m_TotalSize(totalSize == std::numeric_limits<quint64>::max()
                        ? static_cast<quint64>(data.size())
                        : totalSize),
        m_AllowMoreData(allowMoreData)
  {
    if (start < 0 || m_End < start || m_End > data.size() ||
        m_TotalSize < static_cast<quint64>(m_End)) {
      throw std::runtime_error("invalid Starfield save range");
    }
  }

  quint8 readU8()
  {
    require(1);
    return static_cast<quint8>(
        static_cast<unsigned char>(m_Data.at(m_Position++)));
  }

  quint16 readU16()
  {
    return static_cast<quint16>(readUnsigned(2));
  }

  quint32 readU32()
  {
    return static_cast<quint32>(readUnsigned(4));
  }

  quint64 readU64()
  {
    return readUnsigned(8);
  }

  float readFloat()
  {
    quint32 bits = readU32();
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  QString readString()
  {
    const quint16 length = readU16();
    require(length);
    const QString value =
        QString::fromUtf8(m_Data.constData() + m_Position, length);
    m_Position += length;
    return value;
  }

  qsizetype position() const { return m_Position; }
  qsizetype remaining() const { return m_End - m_Position; }

  void seek(qsizetype position)
  {
    if (position < m_Position || position > m_End) {
      throw std::runtime_error("invalid Starfield save offset");
    }
    m_Position = position;
  }

private:
  void require(quint64 length) const
  {
    const quint64 available = static_cast<quint64>(m_End - m_Position);
    if (length > available) {
      const quint64 position = static_cast<quint64>(m_Position);
      if (m_AllowMoreData && position <= m_TotalSize &&
          length <= m_TotalSize - position) {
        throw NeedMoreData();
      }
      throw std::runtime_error("unexpected end of Starfield save");
    }
  }

  quint64 readUnsigned(unsigned width)
  {
    require(width);
    quint64 value = 0;
    for (unsigned i = 0; i < width; ++i) {
      value |= static_cast<quint64>(
                   static_cast<unsigned char>(m_Data.at(m_Position++)))
               << (i * 8);
    }
    return value;
  }

  QByteArray const& m_Data;
  qsizetype m_Position;
  qsizetype m_End;
  quint64 m_TotalSize;
  bool m_AllowMoreData;
};

struct StarfieldSaveData
{
  unsigned long saveNumber = 0;
  unsigned char saveVersion = 0;
  QString playerName;
  unsigned short playerLevel = 0;
  QString playerLocation;
  FILETIME creationTime{};
  QStringList plugins;
  QStringList lightPlugins;
  QStringList mediumPlugins;
};

QByteArray readFileRange(QFile& file, quint64 offset, qsizetype length)
{
  if (offset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
      !file.seek(static_cast<qint64>(offset))) {
    throw std::runtime_error("invalid Starfield save offset");
  }
  const QByteArray data = file.read(length);
  if (data.size() != length) {
    throw std::runtime_error("unexpected end of Starfield save");
  }
  return data;
}

StarfieldSaveData parseSave(QByteArray const& data, quint64 totalSize);

StarfieldSaveData readBcpsSave(QFile& file)
{
  if (file.size() < static_cast<qint64>(BcpsHeaderSize)) {
    throw std::runtime_error("invalid Starfield BCPS container");
  }
  const QByteArray header =
      readFileRange(file, 0, static_cast<qsizetype>(BcpsHeaderSize));
  if (std::memcmp(header.constData(), "BCPS", 4) != 0) {
    throw std::runtime_error("invalid Starfield BCPS container");
  }
  if (readLittleEndian(header, 4, 4) != 1) {
    throw std::runtime_error("unsupported Starfield BCPS container version");
  }

  const quint64 fileSize  = static_cast<quint64>(file.size());
  const quint64 table     = readLittleEndian(header, 0x08, 8);
  const quint64 reserved  = readLittleEndian(header, 0x10, 8);
  const quint64 dataStart = readLittleEndian(header, 0x18, 8);
  const quint64 totalSize = readLittleEndian(header, 0x20, 8);
  const quint64 maxSize   = readLittleEndian(header, 0x28, 8);
  const quint64 chunkSize = readLittleEndian(header, 0x30, 8);
  const quint64 alignment = readLittleEndian(header, 0x38, 8);

  if (std::memcmp(header.constData() + 0x44, "ZIP ", 4) != 0 ||
      alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      reserved % alignment != 0 || dataStart % alignment != 0 ||
      chunkSize < 0x10000 || (chunkSize & (chunkSize - 1)) != 0 ||
      (chunkSize & 0xFFFF) != 0 || maxSize < chunkSize || totalSize == 0 ||
      totalSize > MaxUncompressedSaveSize ||
      totalSize > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
    throw std::runtime_error("invalid Starfield BCPS container header");
  }

  const quint64 chunkCount =
      totalSize / chunkSize + (totalSize % chunkSize == 0 ? 0 : 1);
  if (table < BcpsHeaderSize || table > fileSize ||
      chunkCount > (fileSize - table) / sizeof(quint32) ||
      table + chunkCount * sizeof(quint32) > dataStart ||
      dataStart > fileSize) {
    throw std::runtime_error("invalid Starfield BCPS chunk table");
  }

  const quint64 tableSize = chunkCount * sizeof(quint32);
  const QByteArray chunkTable =
      readFileRange(file, table, static_cast<qsizetype>(tableSize));
  QByteArray output;
  output.reserve(static_cast<qsizetype>(std::min(totalSize, chunkSize)));
  quint64 position = dataStart;
  for (quint64 i = 0; i < chunkCount; ++i) {
    const quint64 compressedSize =
        readLittleEndian(chunkTable, i * sizeof(quint32), sizeof(quint32));
    const quint64 outputSize =
        std::min(chunkSize, totalSize - static_cast<quint64>(output.size()));
    if (compressedSize == 0 || compressedSize > outputSize ||
        position > fileSize || compressedSize > fileSize - position) {
      throw std::runtime_error("invalid Starfield BCPS chunk size");
    }

    const QByteArray compressed =
        readFileRange(file, position, static_cast<qsizetype>(compressedSize));
    if (compressedSize == outputSize) {
      output.append(compressed);
    } else {
      QByteArray decompressed(static_cast<qsizetype>(outputSize), Qt::Uninitialized);
      uLongf decompressedSize = static_cast<uLongf>(outputSize);
      const int result = ::uncompress(
          reinterpret_cast<Bytef*>(decompressed.data()), &decompressedSize,
          reinterpret_cast<Bytef const*>(compressed.constData()),
          static_cast<uLong>(compressedSize));
      if (result != Z_OK || decompressedSize != outputSize) {
        throw std::runtime_error("failed to decompress Starfield BCPS chunk");
      }
      output.append(decompressed);
    }

    if (position > std::numeric_limits<quint64>::max() - compressedSize) {
      throw std::runtime_error("invalid Starfield BCPS chunk offset");
    }
    position += compressedSize;
    const quint64 remainder = position % alignment;
    if (remainder != 0) {
      const quint64 padding = alignment - remainder;
      if (position > std::numeric_limits<quint64>::max() - padding) {
        throw std::runtime_error("invalid Starfield BCPS chunk offset");
      }
      position += padding;
    }

    try {
      return parseSave(output, totalSize);
    } catch (NeedMoreData const&) {
      if (i + 1 == chunkCount) {
        throw std::runtime_error("unexpected end of Starfield save");
      }
    }
  }

  throw std::runtime_error("unexpected end of Starfield save");
}

bool isBaseGamePlugin(QString const& name)
{
  static const QStringList basePlugins = {
      "Constellation.esm", "OldMars.esm",       "ShatteredSpace.esm",
      "SFBGS050.esm",      "SFBGS003.esm",      "SFBGS004.esm",
      "SFBGS006.esm",      "SFBGS007.esm",      "SFBGS008.esm",
      "SFBGS00D.esm",      "SFBGS047.esm",      "Starfield.esm",
      "TestData.esm",      "BlueprintShips",    "BlueprintShips-Starfield.esm",
      "BlueprintShips-TestData.esm"};

  const auto matches = [](QString const& candidate) {
    return std::any_of(basePlugins.cbegin(), basePlugins.cend(),
                       [&candidate](QString const& base) {
                         return candidate.compare(base, Qt::CaseInsensitive) == 0;
                       });
  };
  if (matches(name)) {
    return true;
  }

  const qsizetype dash = name.indexOf('-');
  return dash >= 0 && matches(name.mid(dash + 1));
}

StarfieldSaveData parseSave(QByteArray const& data, quint64 totalSize)
{
  if (data.size() < 12) {
    throw NeedMoreData();
  }
  if (!data.startsWith("SFS_SAVEGAME")) {
    throw std::runtime_error("invalid Starfield save header");
  }

  SaveReader header(data, 12, data.size(), totalSize, true);
  const quint32 headerSize = header.readU32();
  const qsizetype headerStart = header.position();
  if (headerSize > totalSize - static_cast<quint64>(headerStart)) {
    throw std::runtime_error("invalid Starfield save header size");
  }
  if (headerSize > static_cast<quint32>(data.size() - headerStart)) {
    throw NeedMoreData();
  }
  const qsizetype bodyStart = headerStart + headerSize;
  SaveReader headerFields(data, headerStart, bodyStart);

  headerFields.readU32();  // engine version
  StarfieldSaveData result;
  result.saveVersion = headerFields.readU8();
  result.saveNumber = headerFields.readU32();
  result.playerName = headerFields.readString();
  const quint32 level = headerFields.readU32();
  result.playerLevel = static_cast<unsigned short>(level);
  result.playerLocation = headerFields.readString();
  headerFields.readString();  // game date
  headerFields.readString();  // race
  headerFields.readU16();     // gender
  headerFields.readFloat();   // experience
  headerFields.readFloat();   // experience required
  const quint64 fileTime = headerFields.readU64();
  result.creationTime.dwLowDateTime = static_cast<DWORD>(fileTime);
  result.creationTime.dwHighDateTime = static_cast<DWORD>(fileTime >> 32);

  SaveReader body(data, bodyStart, data.size(), totalSize, true);
  body.readU8();           // form version
  body.readString();       // game version
  body.readString();       // created version
  const quint32 pluginInfoSize = body.readU32();
  if (pluginInfoSize >
      totalSize - static_cast<quint64>(body.position())) {
    throw std::runtime_error("invalid Starfield plugin list size");
  }
  if (pluginInfoSize > static_cast<quint32>(body.remaining())) {
    throw NeedMoreData();
  }
  const qsizetype pluginInfoStart = body.position();
  const qsizetype pluginInfoEnd = pluginInfoStart + pluginInfoSize;
  SaveReader plugins(data, pluginInfoStart, pluginInfoEnd);

  const auto readPluginEntries =
      [&plugins](quint32 count, QStringList& destination,
                 unsigned char saveVersion) {
        for (quint32 i = 0; i < count; ++i) {
          const QString name = plugins.readString();
          destination.push_back(name);

          if (saveVersion >= 140) {
            if (plugins.readU8() != 0) {
              plugins.readString();
              plugins.readString();
              plugins.readString();
              plugins.readU8();
            }
          } else if (saveVersion >= 120 && !isBaseGamePlugin(name)) {
            plugins.readString();
            plugins.readString();
            plugins.readString();
            if (saveVersion >= 121) {
              plugins.readU8();
            }
          }
        }
      };

  readPluginEntries(plugins.readU8(), result.plugins, result.saveVersion);
  readPluginEntries(plugins.readU16(), result.lightPlugins, result.saveVersion);
  if (result.saveVersion >= 108) {
    readPluginEntries(plugins.readU32(), result.mediumPlugins,
                      result.saveVersion);
  }
  plugins.seek(pluginInfoEnd);

  return result;
}

StarfieldSaveData readBareSave(QFile& file)
{
  const quint64 totalSize = static_cast<quint64>(file.size());
  if (totalSize < 12 || !file.seek(0)) {
    throw std::runtime_error("invalid Starfield save");
  }

  QByteArray prefix;
  while (static_cast<quint64>(prefix.size()) < totalSize) {
    const quint64 remaining =
        totalSize - static_cast<quint64>(prefix.size());
    const QByteArray next =
        file.read(static_cast<qsizetype>(std::min<quint64>(remaining, 0x10000)));
    if (next.isEmpty()) {
      throw std::runtime_error("unexpected end of Starfield save");
    }
    prefix.append(next);
    try {
      return parseSave(prefix, totalSize);
    } catch (NeedMoreData const&) {
      if (static_cast<quint64>(prefix.size()) == totalSize) {
        throw std::runtime_error("unexpected end of Starfield save");
      }
    }
  }

  throw std::runtime_error("unexpected end of Starfield save");
}

StarfieldSaveData readSaveFile(QFile& file)
{
  if (file.size() < 12) {
    throw std::runtime_error("not a Starfield save");
  }
  const QByteArray magic = readFileRange(file, 0, 12);
  if (magic.startsWith("BCPS")) {
    return readBcpsSave(file);
  }
  if (magic.startsWith("SFS_SAVEGAME")) {
    return readBareSave(file);
  }
  throw std::runtime_error("not a Starfield save");
}

struct SaveCacheEntry
{
  qint64 size = 0;
  qint64 modified = 0;
  StarfieldSaveData save;
};

struct SaveCache
{
  QMutex mutex;
  QHash<QString, SaveCacheEntry> entries;
};

SaveCache& saveCache()
{
  static SaveCache cache;
  return cache;
}

StarfieldSaveData loadSave(QString const& fileName)
{
  const QFileInfo before(fileName);
  QString key = before.canonicalFilePath();
  if (key.isEmpty()) {
    key = before.absoluteFilePath();
  }
  key = QDir::cleanPath(key).toCaseFolded();

  const qint64 size = before.size();
  const qint64 modified = before.lastModified().toMSecsSinceEpoch();
  SaveCache& cache = saveCache();
  if (before.exists()) {
    QMutexLocker lock(&cache.mutex);
    const auto found = cache.entries.constFind(key);
    if (found != cache.entries.cend() && found->size == size &&
        found->modified == modified) {
      return found->save;
    }
  }

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly)) {
    throw std::runtime_error(
        QObject::tr("failed to open %1").arg(fileName).toUtf8().constData());
  }
  const StarfieldSaveData save = readSaveFile(file);

  const QFileInfo after(fileName);
  if (before.exists() && after.size() == size &&
      after.lastModified().toMSecsSinceEpoch() == modified) {
    QMutexLocker lock(&cache.mutex);
    if (!cache.entries.contains(key) && cache.entries.size() >= 1024) {
      cache.entries.erase(cache.entries.begin());
    }
    cache.entries.insert(key, {size, modified, save});
  }
  return save;
}

}  // namespace

StarfieldSaveGame::StarfieldSaveGame(QString const& fileName,
                                     GameStarfield const* game)
    : GamebryoSaveGame(fileName, game, true, true)
{
  const StarfieldSaveData save = loadSave(fileName);
  m_SaveNumber = save.saveNumber;
  m_PCName = save.playerName;
  m_PCLevel = save.playerLevel;
  m_PCLocation = save.playerLocation;
  m_Plugins = save.plugins;
  m_LightPlugins = save.lightPlugins;
  m_MediumPlugins = save.mediumPlugins;

  SYSTEMTIME creationTime;
  ::FileTimeToSystemTime(&save.creationTime, &creationTime);
  setCreationTime(creationTime);
}

std::unique_ptr<GamebryoSaveGame::DataFields>
StarfieldSaveGame::fetchDataFields() const
{
  auto fields = std::make_unique<DataFields>();
  fields->Plugins = m_Plugins;
  fields->LightPlugins = m_LightPlugins;
  fields->MediumPlugins = m_MediumPlugins;
  return fields;
}
