#ifndef ZBSCOREEMAILRESPONSE_H
#define ZBSCOREEMAILRESPONSE_H

#include <stdbool.h>

#include <json.h>

/**
 * @brief Response for GET/POST /scoring (real-time AI scoring).
 */
typedef struct {
    char* email;
    int score;
    bool score_is_set;
} ZBScoreEmailResponse;

ZBScoreEmailResponse new_zb_score_email_response();

char* zb_score_email_response_to_string(ZBScoreEmailResponse* response);

ZBScoreEmailResponse zb_score_email_response_from_json(const json_object* j);

int compare_zb_score_email_response(
    const ZBScoreEmailResponse* response1,
    const ZBScoreEmailResponse* response2
);

#endif
