#define RAPIDJSON_HAS_STDSTRING 1

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>
#include <fcntl.h>

#include <vector>
#include <algorithm>
#include <iomanip>

#include "WwwFormEncodedDict.h"
#include "HttpClient.h"

#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

using namespace std;
using namespace rapidjson;

int API_SERVER_PORT = 8080;
string API_SERVER_HOST = "localhost";
string PUBLISHABLE_KEY = "";

string auth_token;
string user_id;

// Prints a generic error message
void printError() {
    string error = "Error\n";
    write(STDERR_FILENO, error.c_str(), error.length());
}

// Converts from cents to dollars because the server always operates in cents
string cToD(int cents) {
    double dollars = cents / 100;

    stringstream s;
    s << fixed << setprecision(2) << dollars;
    string dollarsAsString = "$" + s.str();

    return dollarsAsString;
}

// Converts from dollars to cents
int dToC(string &dollarsAsString) {
    int dollars = stoi(dollarsAsString);
    int cents = dollars * 100;
    return cents;
}

// Shows the current user's balance
void balance() {
    HttpClient client(API_SERVER_HOST.c_str(), API_SERVER_PORT, false);
    client.set_header("x-auth-token", auth_token);

    // Set the path for get API call
    string p = "/users/" + user_id;
    auto *clientResponse = client.get(p);
    int s = clientResponse->status();

    // Retrieved balance successfully, print balance
    if (s == 200) {

        // This method converts the HTTP body into a rapidjson document
        Document *d = clientResponse->jsonBody();

        // Set balance in correct format and print
        string msg = "Balance: " + cToD((*d)["balance"].GetInt());
        msg.append("\n");
        write(STDOUT_FILENO, msg.c_str(), msg.length());

        delete d;
    }
    else {
        printError();
    }
}

// Authenticates the user, saving the auth_token and user_id for future APIs
void auth(string &username, string &password, string &email) {
    HttpClient client(API_SERVER_HOST.c_str(), API_SERVER_PORT, false);

    // Set the path for post API call for authentication
    string p = "/auth-tokens";

    // Set user account login info in Stripe
    WwwFormEncodedDict body;
    body.set("username", username);
    body.set("password", password);
    body.set("email", email);
    string encodedBody = body.encode();

    // Creates new user if username doesn't exist, or verifies that user exists and logs in the user if password matches
    auto *clientResponse = client.post(p, encodedBody);
    int s = clientResponse->status();

    // Authentication was successful: User logged in or created new account, respectively
    if (s == 200 || s == 201) {

        // This method converts the HTTP body into a rapidjson document
        Document *d = clientResponse->jsonBody();

        // Store auth_token and user_id and print balance to console
        auth_token = (*d)["auth_token"].GetString();
        user_id = (*d)["user_id"].GetString();
        balance();

        delete d;
    }
    else {
        printError();
    }
}

// Deposit accepts in credit card info, charges it, and deposits the specified amount into account
void deposit(int cents, string &creditCardNumber, int expirationYear, int expirationMonth, int cvc) {

    // Call Stripe to create a card token for the credit card info
    HttpClient clientStripe("api.stripe.com", 443, true);
    clientStripe.set_header("Authorization", string("Bearer ") + PUBLISHABLE_KEY);

    // Set the path to the card token
    string stripeClientPath = "/v1/tokens";

    // Set card info in Stripe
    WwwFormEncodedDict sBody;
    sBody.set("card[number]", creditCardNumber);
    sBody.set("card[exp_year]", expirationYear);
    sBody.set("card[exp_month]", expirationMonth);
    sBody.set("card[cvc]", cvc);
    string stripeEncodedBody = sBody.encode();

    // Get Stripe credit card token
    auto clientResponseStripe = clientStripe.post(stripeClientPath, stripeEncodedBody);
    int s = clientResponseStripe->status();

    // Failed to get token
    if (s != 200) {
        printError();
        return;
    }

    // This method converts the HTTP body into a rapidjson document
    auto *d = clientResponseStripe->jsonBody();
    string stripeToken = (*d)["id"].GetString();
    delete d;

    // Create the user client (not to be confused with the stripe client)
    HttpClient client(API_SERVER_HOST.c_str(), API_SERVER_PORT, false);
    client.set_header("x-auth-token", auth_token);

    // Set the path for POST API call for deposits
    string p = "/deposits";

    // Set the amount that user wants to deposit
    WwwFormEncodedDict body;
    body.set("amount", cents);
    body.set("stripe_token", stripeToken);
    string encodedBody = body.encode();

    auto *response = client.post(p, encodedBody);
    s = response->status();

    // When we retrieve the balance successfully, print balance
    if (s == 200) {

        // This method converts the HTTP body into a rapidjson document
        auto *bDoc = response->jsonBody();

        // Print the balance to the console
        string msg = "Balance: " + cToD((*bDoc)["balance"].GetInt());
        msg.append("\n");
        write(STDOUT_FILENO, msg.c_str(), msg.length());

        delete bDoc;
    }
    else {
        printError();
    }
}

// Sends money to another user using that person's username
void send(string &username, int cents) {
    HttpClient client(API_SERVER_HOST.c_str(), API_SERVER_PORT, false);
    client.set_header("x-auth-token", auth_token);

    // Set the path for post API call for transfers
    string path = "/transfers";

    // Set the info for transfer
    WwwFormEncodedDict body;
    body.set("to", username);
    body.set("amount", cents);
    string encodedBody = body.encode();

    // Transfers money from your account to another user
    // You must make sure that there aren't any negative balances and that the to username exists for this API call to succeed
    auto *clientResponse = client.post(path, encodedBody);
    int s = clientResponse->status();

    // Transferred money successfully: print balance
    if (s == 200) {

        // This method converts the HTTP body into a rapidjson document
        Document *d = clientResponse->jsonBody();

        // Print new Balance to Console
        string msg = "Balance: " + cToD((*d)["balance"].GetInt());
        msg.append("\n");
        write(STDOUT_FILENO, msg.c_str(), msg.length());

        delete d;
    }
    else {
        printError();
    }
}

// Updates user email on their account
void updateUserEmail(string &newEmail) {
    HttpClient client(API_SERVER_HOST.c_str(), API_SERVER_PORT, false);

    // Set the path for put API call
    string path = "/users/" + user_id;

    // Set info for new email address
    WwwFormEncodedDict body;
    body.set("email", newEmail);
    string encodedBody = body.encode();

    auto *clientResponse = client.put(path, encodedBody);
    int s = clientResponse->status();

    // Got balance successfully: print balance and updated email address
    if (s == 200) {

        // This method converts the HTTP body into a rapidjson document
        Document *d = clientResponse->jsonBody();

        string msg = "Balance: " + cToD((*d)["balance"].GetInt());
        string updatedEmailAddress = "Updated Email: " + string((*d)["email"].GetString());

        delete d;
    }
    else {
        printError();
    }
}

// Logout deletes the current auth_token from the server, and exits the dcash wallet process
void logout() {
    HttpClient client(API_SERVER_HOST.c_str(), API_SERVER_PORT, false);
    client.set_header("x-auth-token", auth_token);

    // Set the path for DELETE API call
    string p = "/auth-tokens/" + auth_token;
    auto *response = client.del(p);
    int s = response->status();

    // Failed log out: print error
    if (s != 200) {
        printError();
    }
}

// Run valid user command
void runCommand(vector<string> &inputs) {

    // Initialize first argument and remove it from the original commands string
    string command = inputs[0];
    inputs.erase(inputs.begin());

    // Identify the different commands and call their respective functions
    // Auth Command
    if (command == "auth") {
        if (inputs.size() == 3) {
            auth(inputs[0], inputs[1], inputs[2]);
        }
        else {
            printError();
        }
    }

    // Balance Command
    else if (command == "balance") {
        if (inputs.empty() && !auth_token.empty()) {
            balance();
        }
        else {
            printError();
        }
    }

    // Deposit Command
    else if (command == "deposit") {
        if (inputs.size() == 5 && !auth_token.empty()) {
            int cents = dToC(inputs[0]);

            if (cents > 0) {
                deposit(cents, inputs[1], stoi(inputs[2]), stoi(inputs[3]), stoi(inputs[4]));
            }
            else {
                printError();
            }
        }
        else {
            printError();
        }
    }

    // Send Command
    else if (command == "send") {
        if (inputs.size() == 2 && !auth_token.empty()) {
            int cents = dToC(inputs[1]);

            if (cents > 0) {
                send(inputs[0], cents);
            }
            else  {
                printError();
            }
        }
        else {
            printError();
        }
    }

    // Update Command
    else if (command == "update") {
        if (inputs.size() == 1 && !auth_token.empty()) {
            updateUserEmail(inputs[0]);
        }
        else {
            printError();
        }
    }

    // Logout Command
    else if (command == "logout") {
        if (inputs.empty()) {
            logout();
            exit(0);
        }
        else {
            printError();
        }
    }
    else {
        printError();
    }
}

// Parse and run user input from command line
void parseAndRunUin(string &uin) {
    vector<string> stringToken;
    string space = " ";
    string tab = "\t";
    string argument;

    stringstream spaceStream(uin);
    string spaceToken;

    // Parse the input for spaces, denoted by the ' '
    while (getline(spaceStream, spaceToken, ' ')) {
        stringstream tabStream(spaceToken);
        string tabToken;

        // Parse the input for tabs, denoted by the '\t'
        while (getline(tabStream, tabToken, '\t')) {
            if (tabToken.length() > 0) {
                string input = tabToken;

                // Discard any excessive spaces and tabs in the input
                input.erase(remove(input.begin(), input.end(), ' '), input.end());
                input.erase(remove(input.begin(), input.end(), '\t'), input.end());

                stringToken.emplace_back(input.c_str());
            }
        }
    }

    // Run the command and clear the input for next run through
    if (!stringToken.empty()) {
        runCommand(stringToken);
        stringToken.clear();
    }
    // Bad input
    else {
        printError();
    }
}

// Parse inputted file
void parseFile(int &fd) {
    string line;
    int length;
    char buffer[4096];

    // Read one byte at a time until we reach eof
    while ((length = read(fd, buffer, 4096)) > 0) {
        for (int idx = 0; idx < length; idx++) {

            // Discard empty lines
            if (buffer[idx] == '\n') {
                line.erase(remove(line.begin(), line.end(), '\n'), line.end());

                // Parse each line for spaces and tabs, then run the remaining command
                parseAndRunUin(line);

                // Clear line for next run through
                line.clear();
            }
            // Add each byte to the line until we run into another '\n'
            else {
                line += buffer[idx];
            }
        }
    }
    // Making sure we check last line of file
    if (line.length() > 0) {

        // Discard empty lines
        line.erase(remove(line.begin(), line.end(), '\n'), line.end());

        // Parse each line for spaces and tabs, then run the remaining command
        parseAndRunUin(line);

        // Clear line for next run through
        line.clear();
    }
}

int main(int argc, char *argv[]) {
    stringstream config;
    int fd = open("config.json", O_RDONLY);
    if (fd < 0) {
        cout << "could not open config.json" << endl;
        exit(1);
    }
    int ret;
    char buffer[4096];
    while ((ret = read(fd, buffer, sizeof(buffer))) > 0) {
        config << string(buffer, ret);
    }
    Document d;
    d.Parse(config.str());
    API_SERVER_PORT = d["api_server_port"].GetInt();
    API_SERVER_HOST = d["api_server_host"].GetString();
    PUBLISHABLE_KEY = d["stripe_publishable_key"].GetString();

    // Command Line program to interact with users:
    // Interactive Mode:
    if (argc == 1) {
        while (true) {
            // Prompt User
            string uin;
            cout << "D$> ";

            // Wait for input
            while (uin.empty()) {
                getline(cin, uin);
            }

            // Parse and run the input
            if (!uin.empty()) {
                parseAndRunUin(uin);
            }
            else {
                printError();
            }
        }
    }

    // Batch Mode:
    else if (argc == 2) {
        int fileDescriptor;
        fileDescriptor = open(argv[1], O_RDONLY);

        // Failed to open file: print error and exit
        if (fileDescriptor == -1) {
            printError();
            exit(1);
        }

        // Parse and run commands in file
        parseFile(fileDescriptor);
        close(fileDescriptor);
    }

    // Bad Input: print error and exit
    else {
        printError();
        exit(1);
    }

    return 0;
}
