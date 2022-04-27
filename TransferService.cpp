#define RAPIDJSON_HAS_STDSTRING 1

#include <unistd.h>

#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "TransferService.h"
#include <HttpClient.h>

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;
using namespace std;

TransferService::TransferService() : HttpService("/transfers") { }

// Transfers money from your account to another user
void TransferService::post(HTTPRequest *request, HTTPResponse *response) {

    // Print Message to Console
    string msg = "Transferring amount";
    msg.append("\n");
    write(STDOUT_FILENO, msg.c_str(), msg.length());

    string stripeToken = request->getHeader("x-auth-token");

    // Set tokens for transfer amount and receiver's username
    WwwFormEncodedDict encodedBody = request->formEncodedBody();
    int amountCents = stoi(encodedBody.get("amount"));
    string usernameOfReceiver = encodedBody.get("to");

    auto senderTokens = m_db->auth_tokens.find(stripeToken);

    // Error: User not authenticated
    if (senderTokens == m_db->auth_tokens.end()) {
        response->setStatus(401);
        return;
    }

    // Assign Sender's tokens
    auto userSendingMoney = senderTokens->second;

    auto receiver = m_db->users.find(usernameOfReceiver);

    // Error: Receiver not found
    if (receiver == m_db->users.end()) {
        response->setStatus(401);
        return;
    }

    // Assign Receiver's information
    auto userReceivingMoney = receiver->second;

    // Error: Transfer amount is negative
    if (amountCents <= 0) {
        response->setStatus(400);
        return;
    }

    // Error: Sender has insufficient funds
    if (amountCents > userSendingMoney->balance) {
        response->setStatus(400);
        return;
    }

    // Set the sender path
    string sPath = "/v1/customers/" + userSendingMoney->user_id + "/balance_transactions";

    // Set sender amount on Stripe
    WwwFormEncodedDict sBody;
    sBody.set("amount", 0 - amountCents);
    sBody.set("currency", "usd");
    string senderEncodedBody = sBody.encode();

    HttpClient clientSender("api.stripe.com", 443, true);
    clientSender.set_basic_auth(m_db->stripe_secret_key, "");
    auto *senderResponse = clientSender.post(sPath, senderEncodedBody);

    int s = senderResponse->status();
    response->setStatus(s);

    // Error: Sender transfer was unsuccessful
    if (s != 200) {
        string error = "Error\n";
        write(STDERR_FILENO, error.c_str(), error.length());
        return;
    }

    // Set the receiver path
    string rPath = "/v1/customers/" + userReceivingMoney->user_id + "/balance_transactions";

    // Set receiver 's expected amount on Stripe
    WwwFormEncodedDict rBody;
    rBody.set("amount", amountCents);
    rBody.set("currency", "usd");
    string receiverEncodedBody = rBody.encode();

    HttpClient receiveClient("api.stripe.com", 443, true);
    receiveClient.set_basic_auth(m_db->stripe_secret_key, "");
    auto *receiverResponse = receiveClient.post(rPath, receiverEncodedBody);

    s = receiverResponse->status();
    response->setStatus(s);

    // Error: Receiver transfer was unsuccessful
    if (s != 200) {
        string error = "Error\n";
        write(STDERR_FILENO, error.c_str(), error.length());
        return;
    }

    // Update transfer info in sender's account
    auto *transferAmount = new Transfer();
    transferAmount->amount = amountCents;
    transferAmount->from = userSendingMoney;
    transferAmount->to = userReceivingMoney;
    m_db->transfers.push_back(transferAmount);

    Document *sJsonDoc = senderResponse->jsonBody();
    Document *rJsonDoc = receiverResponse->jsonBody();

    // Update Sender's Balance
    userSendingMoney->balance = (*sJsonDoc)["ending_balance"].GetInt();

    // Update Receiver's Balance
    userReceivingMoney->balance = (*rJsonDoc)["ending_balance"].GetInt();

    // use rapidjson to create a return object
    Document document;
    Document::AllocatorType &a = document.GetAllocator();
    Value o;
    o.SetObject();

    // add a key value pair directly to the object
    o.AddMember("balance", userSendingMoney->balance, a);

    Value array;
    array.SetArray();

    // Send transfer history
    auto transfers = m_db->transfers;
    for (int i = 0; i < transfers.size(); i++) {
        if (transfers[i]->from == userSendingMoney || transfers[i]->to == userSendingMoney) {
            Value to;
            to.SetObject();
            to.AddMember("from", transfers[i]->from->username, a);
            to.AddMember("to", transfers[i]->to->username, a);
            to.AddMember("amount", transfers[i]->amount, a);
            array.PushBack(to, a);
        }
    }
    o.AddMember("transfers", array, a);

    // now some rapidjson boilerplate for converting the JSON object to a string
    document.Swap(o);
    StringBuffer buffer;
    PrettyWriter<StringBuffer> writer(buffer);
    document.Accept(writer);

    // set the return object
    response->setContentType("application/json");
    response->setBody(buffer.GetString() + string("\n"));
}
