
namespace
{
    int64 getRandomSeedFromFreeVolume()
    {
        Random r( File::getCurrentWorkingDirectory().getBytesFreeOnVolume() );

        return r.nextInt64();
    }
}


//==============================================================================
Uuid::Uuid()
{
    // The normal random seeding is pretty good, but we'll throw current free hard disk volume
    // into the mix too, to make it very very unlikely that two UUIDs will ever be the same..

    static Random r1 (getRandomSeedFromFreeVolume());
    Random r2;

    for (size_t i = 0; i < sizeof (uuid); ++i)
        uuid[i] = (uint8) (r1.nextInt() ^ r2.nextInt());
}

Uuid::~Uuid() noexcept {}

Uuid::Uuid (const Uuid& other) noexcept
{
    memcpy (uuid, other.uuid, sizeof (uuid));
}

Uuid& Uuid::operator= (const Uuid& other) noexcept
{
    memcpy (uuid, other.uuid, sizeof (uuid));
    return *this;
}

bool Uuid::operator== (const Uuid& other) const noexcept    { return memcmp (uuid, other.uuid, sizeof (uuid)) == 0; }
bool Uuid::operator!= (const Uuid& other) const noexcept    { return ! operator== (other); }

bool Uuid::isNull() const noexcept
{
    for (size_t i = 0; i < sizeof (uuid); ++i)
        if (uuid[i] != 0)
            return false;

    return true;
}

String Uuid::toString() const
{
    return String::toHexString (uuid, sizeof (uuid), 0);
}

Uuid::Uuid (const String& uuidString)
{
    operator= (uuidString);
}

Uuid& Uuid::operator= (const String& uuidString)
{
    MemoryBlock mb;
    mb.loadFromHexString (uuidString);
    mb.ensureSize (sizeof (uuid), true);
    mb.copyTo (uuid, 0, sizeof (uuid));
    return *this;
}

Uuid::Uuid (const uint8* const rawData)
{
    operator= (rawData);
}

Uuid& Uuid::operator= (const uint8* const rawData) noexcept
{
    if (rawData != nullptr)
        memcpy (uuid, rawData, sizeof (uuid));
    else
        zeromem (uuid, sizeof (uuid));

    return *this;
}
