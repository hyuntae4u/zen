#ifndef _SIDECHAINFORK_H
#define _SIDECHAINFORK_H

#include "fork5_shieldfork.h"
#include "primitives/block.h"

namespace zen {

class SidechainFork : public ShieldFork
{
public:
    SidechainFork();

    /**
	 * @brief returns sidechain tx version based on block height, if sidechains are not supported return 0
	 */
	inline virtual int getSidechainTxVersion() const { return SC_TX_VERSION; }

    /**
	 * @brief returns true if sidechains are supported
	 */
	inline virtual bool areSidechainsSupported() const { return true; }

    /**
	 * @brief returns new block version
	 */
	inline virtual int getNewBlockVersion() const { return CBlock::SC_CERT_BLOCK_VERSION; }

    /**
	 * @brief returns true if the block version is valid at this fork
	 */
    inline virtual bool isValidBlockVersion(int nVersion) const { return (nVersion == CBlock::SC_CERT_BLOCK_VERSION); }
};

}
#endif // _SIDECHAINFORK_H