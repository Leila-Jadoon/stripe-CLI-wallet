#define RAPIDJSON_HAS_STDSTRING 1

#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "AccountService.h"
#include <HttpClient.h>

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

using namespace std;
using namespace rapidjson;

AccountService::AccountService() : HttpService("/users") {}

// Fetches the User object for the account
void AccountService::get(HTTPRequest *request, HTTPResponse *response) {

    string token = request->getHeader("x-auth-token");
    string userID = request->getPathComponents().back();

    auto users = m_db->auth_tokens.find(token);

    // Error: User has not been authenticated
    if (users== m_db->auth_tokens.end()) {
        response->setStatus(401);
        return;
    }

    // Error: Token - User mismatch
    if (userID!= users->second->user_id) {
        response->setStatus(401);
        return;
    }

    // Error: Missing parameters
    if (userID.empty() || token.empty()) {
        response->setStatus(400);
        return;
    }

    auto *u =users->second;

    HttpClient client("api.stripe.com", 443, true);
    client.set_basic_auth(m_db->stripe_secret_key, "");

    string p = "/v1/customers/" + userID;
    auto *clientResponse = client.get(p);
    int s = clientResponse->status();
    response->setStatus(s);

    if (s == 200) {

        Document *d = clientResponse->jsonBody();
        u->balance = (*d)["balance"].GetInt();
        delete d;

        string auth_token = StringUtils::createAuthToken();
        m_db->auth_tokens.insert(pair<string, User*>(auth_token, u));

        // use rapidjson to create a return object
        Document document;
        Document::AllocatorType &a = document.GetAllocator();
        Value o;
        o.SetObject();

        // add a key value pair directly to the object
        o.AddMember("email", u->email, a);
        o.AddMember("balance", u->balance, a);

        // now some rapidjson boilerplate for converting the JSON object to a string
        document.Swap(o);
        StringBuffer buffer;
        PrettyWriter<StringBuffer> writer(buffer);
        document.Accept(writer);

        // set the return object
        response->setContentType("application/json");
        response->setBody(buffer.GetString() + string("\n"));
    }
    else {
        string error = "Error\n";
        write(STDOUT_FILENO, error.c_str(), error.length());
    }
}

// Updates the information for a user
void AccountService::put(HTTPRequest *request, HTTPResponse *response) {

    WwwFormEncodedDict encodedBody = request->formEncodedBody();
    string email = encodedBody.get("email");
    string token = request->getHeader("x-auth-token");
    string userID = request->getPathComponents().back();

    auto users = m_db->auth_tokens.find(token);

    // Error: User has not been authenticated
    if (users== m_db->auth_tokens.end()) {
        response->setStatus(401);
        return;
    }

    // Error: Token - User mismatch
    if (userID!= users->second->user_id) {
        response->setStatus(401);
        return;
    }

    // Error: Missing Parameters
    if (userID.empty() || token.empty() || email.empty()) {
        response->setStatus(400);
        return;
    }

    auto *u = users->second;

    HttpClient client("api.stripe.com", 443, true);
    client.set_basic_auth(m_db->stripe_secret_key, "");

    string p = "/v1/customers/" + userID;

    WwwFormEncodedDict updateEncodedBody;
    updateEncodedBody.set("email", email);
    string updatedBody = updateEncodedBody.encode();

    auto *clientResponse = client.post(p, updatedBody);
    int s = clientResponse->status();
    response->setStatus(s);

    // Upon successful Stripe request, update the user info
    if (s== 200) {
        auto *d = clientResponse->jsonBody();
        u->email = (*d)["email"].GetString();
        u->balance = (*d)["balance"].GetInt();
        delete d;

        string auth_token = StringUtils::createAuthToken();
        m_db->auth_tokens.insert(pair<string, User*>(auth_token, u));

        // use rapidjson to create a return object
        Document document;
        Document::AllocatorType &a = document.GetAllocator();
        Value o;
        o.SetObject();

        // add a key value pair directly to the object
        o.AddMember("email", u->email, a);
        o.AddMember("balance", u->balance, a);

        // now some rapidjson boilerplate for converting the JSON object to a string
        document.Swap(o);
        StringBuffer buffer;
        PrettyWriter<StringBuffer> writer(buffer);
        document.Accept(writer);

        // set the return object
        response->setContentType("application/json");
        response->setBody(buffer.GetString() + string("\n"));
    }
    else {
        string error = "Error\n";
        write(STDOUT_FILENO, error.c_str(), error.length());
    }
}