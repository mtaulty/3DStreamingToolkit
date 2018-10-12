#pragma once

class ImageMultiPeerConductor : public MultiPeerConductor
{
public:
	ImageMultiPeerConductor(
		shared_ptr<FullServerConfig> config) : MultiPeerConductor(config)
	{

	};

	scoped_refptr<PeerConductor> SafeAllocatePeerMapEntry(int peer_id)
	{
		// TODO: write some code here.
		return(nullptr);
	}
};
