#define RAPIDJSON_HAS_STDSTRING 1

#include <stdio.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "AuthService.h"
#include "StringUtils.h"
#include <HttpClient.h>

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

using namespace std;
using namespace rapidjson;

AuthService::AuthService() : HttpService("/auth-tokens") { }

// If the username doesn't exist this call will create a new user, and if it does exist
// then it logs in the user if the password matches
void AuthService::post(HTTPRequest *request, HTTPResponse *response) {

    // Set the user log in info in Stripe
    WwwFormEncodedDict encodedBody = request->formEncodedBody();
    string username = encodedBody.get("usermame");
    string password = encodedBody.get("password");

    auto usernameInChars = username.c_str();

    // Error: Uppercase letter found in username
    for (size_t i =0; i< strlen(usernameInChars); i++) {
        if (isupper(usernameInChars[i])) {
            response->setStatus(400);
            return;
        }
    }

    HttpClient client("api.stripe.com", 443, true);
    client.set_basic_auth(m_db->stripe_secret_key,"");
    auto userInfo = m_db->users.find(username);

    string p;
    WwwFormEncodedDict body;
    User *u;
    bool isNewUser = false;

    // User Exists: check for matching username and password
    if (userInfo != m_db->users.end()) {
        u = userInfo->second;

        if (u->password == password) {
            string userID = u->user_id;
            p = "/v1/customers/" + userID;
        }
        else {
            response->setStatus(400);
            return;
        }
    }
    // New User: save username and password for the new account
    else {
        isNewUser = true;
        p = "/v1/customers";
        u = new User();
        u->username = username;
        u->password = password;

        m_db->users.insert(pair<string, User*> (username, u));
    }

    string eBody = body.encode();
    auto *clientResponse = client.post(p, eBody);
    int status = clientResponse->status();
    response->setStatus(status);

    // Successful Stripe request of user info (new or existing)
    if (status == 200) {
        if (isNewUser) {
            response->setStatus(201);
        }

        // Obtain user id and balance
        Document *d = clientResponse->jsonBody();
        u->user_id = (*d)["id"].GetString();
        u->balance = (*d)["balance"].GetInt();

        delete d;

        string auth_token = StringUtils::createAuthToken();
        m_db->auth_tokens.insert(pair<string, User*>(auth_token, u));

        // Use rapidjson to create a return object
        Document document;
        Document::AllocatorType &a = document.GetAllocator();
        Value o;
        o.SetObject();

        // Add a key value pair directly to the object
        o.AddMember("auth_token", auth_token, a);
        o.AddMember("user_id", u->user_id, a);

        // Now some rapidjson boilerplate for converting the JSON object to a string
        document.Swap(o);
        StringBuffer buffer;
        PrettyWriter<StringBuffer> writer(buffer);
        document.Accept(writer);

        // Set the return object
        response->setContentType("application/json");
        response->setBody(buffer.GetString() + string("\n"));
    }
    else {
        string error = "Error\n";
        write(STDOUT_FILENO, error.c_str(), error.length());
    }
}

// Deletes the auth token, used to logout a user. The user can delete any auth_token
// for their account, even if they authenticate with a different auth_token.
void AuthService::del(HTTPRequest *request, HTTPResponse *response) {
    string token = request->getHeader("x-auth-token");
    string delToken = request->getPathComponents().back();
    auto tokens = m_db->auth_tokens.find(token);

    // Error: user not authenticated
    if (tokens == m_db->auth_tokens.end()) {
        response->setStatus(401);
        return;
    }

    auto user = tokens->second;
    auto delTokens = m_db->auth_tokens.find(delToken);

    // Delete token  for this account
    if (delTokens != m_db->auth_tokens.end()) {

        // Verify that the token belongs to the user or throw error
        if (delTokens->second == user) {
            m_db->auth_tokens.erase(delTokens->first);
        }
        else {
            response->setStatus(401);
            return;
        }
        response->setStatus(200);
    }

    // Error: Token not found
    else {
        response->setStatus(401);
        return;
    }

    response->setStatus(200);
}