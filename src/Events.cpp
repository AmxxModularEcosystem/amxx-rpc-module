#include "Events.h"

#include "RpcDispatch.h"
#include "Transport.h"

#include <map>
#include <set>
#include <vector>

namespace {

// event name -> set of subscribed session ids
std::map<std::string, std::set<uint64_t> > g_subscriptions;

} // namespace

void Events_Init() {
	g_subscriptions.clear();
}

void Events_Shutdown() {
	g_subscriptions.clear();
}

bool Events_Subscribe(uint64_t sessionId, const std::string& event) {
	if (event.empty())
		return false;
	g_subscriptions[event].insert(sessionId);
	return true;
}

bool Events_Unsubscribe(uint64_t sessionId, const std::string& event) {
	if (event.empty())
		return false;
	std::map<std::string, std::set<uint64_t> >::iterator it = g_subscriptions.find(event);
	if (it == g_subscriptions.end())
		return false;
	it->second.erase(sessionId);
	if (it->second.empty())
		g_subscriptions.erase(it);
	return true;
}

void Events_Emit(const std::string& event, const std::string& payloadJson) {
	if (event.empty())
		return;
	std::map<std::string, std::set<uint64_t> >::iterator it = g_subscriptions.find(event);
	if (it == g_subscriptions.end())
		return;
	for (std::set<uint64_t>::iterator sit = it->second.begin(); sit != it->second.end();) {
		if (!Transport_IsSessionAlive(*sit)) {
			sit = it->second.erase(sit); // lazy pruning
			continue;
		}
		Rpc_PushNotification(*sit, event, payloadJson);
		++sit;
	}
	if (it->second.empty())
		g_subscriptions.erase(it);
}

void Events_Prune() {
	for (std::map<std::string, std::set<uint64_t> >::iterator it = g_subscriptions.begin();
	     it != g_subscriptions.end();) {
		for (std::set<uint64_t>::iterator sit = it->second.begin(); sit != it->second.end();) {
			if (!Transport_IsSessionAlive(*sit))
				sit = it->second.erase(sit);
			else
				++sit;
		}
		if (it->second.empty())
			it = g_subscriptions.erase(it);
		else
			++it;
	}
}
