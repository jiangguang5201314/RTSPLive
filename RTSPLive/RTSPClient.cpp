#include "RTSPClient.h"
#include "RtpPacket.h"
#include "InterleavedFrame.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
////

RTSPClient::RTSPClient(IRTSPClientSink * sink,
                       uint32_t          id,
                       IUTE            * ute,
                       std::string       host,
                       int               time_out) : m_sink(sink),
                                                     m_ute(ute),
                                                     m_id(id),
                                                     m_host(host)
{

}

RTSPClient::~RTSPClient()
{
    while (!m_streams.empty())
    {
        del_stream(m_streams.front()->stream);
    }
}

bool 
RTSPClient::add_stream(IAVStream * stream, uint16_t host_port, std::string ip, uint16_t port)
{
    XUMutexGuard mon(m_lock);

    std::vector<NetStream *>::iterator itr = m_streams.begin();
    for (; itr != m_streams.end(); ++itr)
    {
        if (stream->stream_id() == (*itr)->stream->stream_id())
        {
            /* already added */
            return false;
        }
    }

    NetStream * netstream = new NetStream;

    netstream->active    = false;
    netstream->sync      = true;
    netstream->stream    = stream;
    netstream->interpkt  = nullptr;
    netstream->transport = m_ute->create_transport(this, m_host, host_port, ip, port);
    if (netstream->transport)
    {
        m_streams.push_back(netstream);
        return true;
    }

    delete netstream;
    return false;
}

bool 
RTSPClient::add_stream(IAVStream * stream, std::shared_ptr<IUTETransport> transport, uint8_t *channelid)
{
    XUMutexGuard mon(m_lock);

    std::vector<NetStream *>::iterator itr = m_streams.begin();
    for (; itr != m_streams.end(); ++itr)
    {
        if (stream->stream_id() == (*itr)->stream->stream_id())
        {
            /* already added */
            return false;
        }
    }

    if (!transport)
    {
        return false;
    }

    NetStream * netstream = new NetStream;

    netstream->active       = false;
    netstream->sync         = true;
    netstream->channelid[0] = channelid[0];
    netstream->channelid[1] = channelid[1];
    netstream->interpkt     = new char[InterleavedFrame::overhead() + RtpPacket::max_packet_size()];
    netstream->stream       = stream;
    netstream->transport    = transport;

    m_streams.push_back(netstream);

    return true;
}

void
RTSPClient::del_stream(IAVStream * stream)
{
    XUMutexGuard mon(m_lock);

    std::vector<NetStream *>::iterator itr = m_streams.begin();
    for (; itr != m_streams.end(); ++itr)
    {
        NetStream * netstream = *itr;
        if (netstream->stream == stream)
        {
            netstream->active = false;
            if (netstream->transport)
            {
                netstream->transport->set_observer(nullptr);
                netstream->transport.reset();
            }
            
            delete[] netstream->interpkt;
            delete netstream;

            m_streams.erase(itr);
            break;
        }
    }
}

void 
RTSPClient::active_stream(IAVStream * stream)
{
    XUMutexGuard mon(m_lock);

    std::vector<NetStream *>::iterator itr = m_streams.begin();
    for (; itr != m_streams.end(); ++itr)
    {
        NetStream * netstream = *itr;
        if (netstream->stream == stream)
        {
            netstream->active = true;
            break;
        }
    }
}

std::string 
RTSPClient::session()
{
    return std::to_string(rand()); ///< Session identifiers are opaque strings of arbitrary length.
}

void 
RTSPClient::on_packet(IAVStream * stream, IRtpPacket * packet)
{
    XUMutexGuard mon(m_lock);

    std::vector<NetStream *>::iterator itr = m_streams.begin();
    for (; itr != m_streams.end(); ++itr)
    {
        NetStream * netstream = *itr;
        if (netstream->stream == stream)
        {
            if (netstream->active)
            {
                ///< if (!netstream->sync) ///< need a random access unit ?
                {
                    if (netstream->transport->type() == UTE_TRANSPORT_TCP)
                    {
                        int frame_len = InterleavedFrame::format(netstream->channelid[0], packet, netstream->interpkt);
                        if (frame_len > 0)
                        {
                            netstream->transport->send(netstream->interpkt, frame_len);
                        }
                    }
                    else
                    {
                        netstream->transport->send(packet->rtp_packet(), packet->rtp_packet_size());
                    }

                    netstream->sync = false;
                }
            }

            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////
