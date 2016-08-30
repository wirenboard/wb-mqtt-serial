#include <gtest/gtest.h>

#include "fake_mqtt.h"

using namespace std;

TFakeMQTTClient::TFakeMQTTClient(const std::string& id, TLoggedFixture& fixture)
    : MQTTId(id), Connected(false), Fixture(fixture)
{
    // NOOP
}

void TFakeMQTTClient::Connect()
{
    Connected = true;

    for (auto observer: GetObservers())
        observer->OnConnect(0);
}

int TFakeMQTTClient::Publish(int *mid,
                             const string& topic,
                             const string& payload,
                             int qos,
                             bool retain)
{
    return DoPublish(false, mid, topic, payload, qos, retain);
}

int TFakeMQTTClient::DoPublish(bool external,
                               int *mid,
                               const string& topic,
                               const string& payload,
                               int qos,
                               bool retain)
{
    if (external)
        Fixture.Note() << "Publish: " << topic << ": '" << payload << "' (QoS " << qos <<
            (retain ? ", retained)" : ")");
    else
        Fixture.Emit() << "Publish: " << topic << ": '" << payload << "' (QoS " << qos <<
            (retain ? ", retained)" : ")");

    if (mid)
        ADD_FAILURE() << "TFakeMQTTClient currently doesn't support non-null mids";
    if (!Connected) {
        ADD_FAILURE() << "TFakeMQTTClient: publishing when not connected";
        return MOSQ_ERR_NO_CONN;
    }

    // TBD: handle topic wildcards
    if (Subscriptions.count(topic)) {
        mosquitto_message msg;
        msg.mid = 0;
        msg.topic = strdup(topic.c_str());
        msg.payload = strdup(payload.c_str());
        msg.payloadlen = payload.size();
        msg.qos = qos;
        msg.retain = retain;
        for (auto observer: GetObservers())
            observer->OnMessage(&msg);
        free(msg.topic);
        free(msg.payload);
    }
    return MOSQ_ERR_SUCCESS;
}

int TFakeMQTTClient::Subscribe(int *mid, const string& sub, int qos)
{
    Fixture.Emit() << "Subscribe: " << sub << " (QoS " << qos << ")";
    if (mid)
        ADD_FAILURE() << "TFakeMQTTClient currently doesn't support non-null mids";
    if (!Connected) {
        ADD_FAILURE() << "TFakeMQTTClient: subscribing when not connected";
        return MOSQ_ERR_NO_CONN;
    }

    Subscriptions.insert(sub);

    for (auto observer: GetObservers())
        observer->OnSubscribe(0, 1, &qos);

    return MOSQ_ERR_SUCCESS;
}
