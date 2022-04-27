#define RAPIDJSON_HAS_STDSTRING 1

#include <unistd.h>

#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "DepositService.h"
#include "Database.h"
#include "HttpClient.h"

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;
using namespace std;

DepositService::DepositService() : HttpService("/deposits") { }

// Uses a credit card to deposit money into your account
void DepositService::post(HTTPRequest *request, HTTPResponse *response) {
    string token = request->getHeader("x-auth-token");

    // Assign tokens for credit cards and deposit amounts
    WwwFormEncodedDict encodedBody = request->formEncodedBody();
    int amountCents = stoi(encodedBody.get("amount"));
    string cardToken = encodedBody.get("stripe_token");
    auto users = m_db->auth_tokens.find(token);

    // Error: User not authenticated
    if (users == m_db->auth_tokens.end()) {
        response->setStatus(401);
        return;
    }
    auto *u = users->second;

    if (amountCents <= 0) {
        response->setStatus(400);
        return;
    }

    // Charge Client
    HttpClient chargeClient("api.stripe.com", 443, true);
    chargeClient.set_basic_auth(m_db->stripe_secret_key, "");

    string p = "/v1/charges";

    // Set information about amounts and currency on Stripe
    WwwFormEncodedDict chargeBody;
    chargeBody.set("amount", amountCents);
    chargeBody.set("source", cardToken);
    chargeBody.set("currency", "usd");
    string encodeBody = chargeBody.encode();

    auto *charge = chargeClient.post(p, encodeBody);
    int s = charge->status();

    response->setStatus(s);

    // Error: Unsuccessful Deposit
    if (s != 200) {
        string error = "Error\n";
        write(STDERR_FILENO, error.c_str(), error.length());
        return;
    }

    Document *jsonDoc = charge->jsonBody();

    // Update User Deposit Information in their account
    auto *depositAmount = new Deposit();
    depositAmount->amount = amountCents;
    depositAmount->stripe_charge_id = (*jsonDoc )["id"].GetString();
    depositAmount->to = u;
    m_db->deposits.push_back(depositAmount);

    delete jsonDoc ;

    // Set the new path
    p = "/v1/customers/" + u->user_id + "/balance_transactions";

    // Set deposit transaction information on Stripe
    WwwFormEncodedDict depositTransaction;
    depositTransaction.set("amount", amountCents);
    depositTransaction.set("currency", "usd");
    string depositTransactionBody = depositTransaction.encode();

    HttpClient clientTransaction("api.stripe.com", 443, true);
    clientTransaction.set_basic_auth(m_db->stripe_secret_key, "");
    auto *clientTransactionResponse = clientTransaction.post(p, depositTransactionBody);

    s = clientTransactionResponse->status();
    response->setStatus(s);

    // Error: Failed to get response from Stripe
    if (s != 200) {
        string error = "Error\n";
        write(STDERR_FILENO, error.c_str(), error.length());
        return;
    }

    Document *transactionDocument = clientTransactionResponse->jsonBody();

    // Update User Balance Information in their account
    u->balance = (*transactionDocument )["ending_balance"].GetInt();
    delete transactionDocument ;

    // use rapidjson to create a return object
    Document document;
    Document::AllocatorType &a = document.GetAllocator();
    Value o;
    o.SetObject();

    // add a key value pair directly to the object
    o.AddMember("balance", u->balance, a);

    Value array;
    array.SetArray();

    // Send the deposit history
    auto deposits = m_db->deposits;
    for (int i = 0; i < deposits.size(); i++) {
        if (deposits[i]->to == u) {
            Value to;
            to.SetObject();
            to.AddMember("to", deposits[i]->to->username, a);
            to.AddMember("amount", deposits[i]->amount, a);
            to.AddMember("stripe_charge_id", deposits[i]->stripe_charge_id, a);
            array.PushBack(to, a);
        }
    }
    o.AddMember("deposits", array, a);

    // now some rapidjson boilerplate for converting the JSON object to a string
    document.Swap(o);
    StringBuffer buffer;
    PrettyWriter<StringBuffer> writer(buffer);
    document.Accept(writer);

    // set the return object
    response->setContentType("application/json");
    response->setBody(buffer.GetString() + string("\n"));
}