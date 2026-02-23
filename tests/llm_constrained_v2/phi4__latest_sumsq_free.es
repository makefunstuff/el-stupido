-stupido
{
    "main_body": [
        {
            "statement_type": "assignment",
            "variable": "acc",
            "value": 0
        },
        {
            "statement_type": "for_loop",
            "loop_variable": "i",
            "range_start": 1,
            "range_end": 100,
            "body": [
                {
                    "statement_type": "assignment",
                    "variable": "acc",
                    "operation": "+=",
                    "expression": {
                        "type": "multiplication",
                        "left": "i",
                        "right": "i"
                    }
                }
            ]
        },
        {
            "statement_type": "print",
            "expr": "acc"
        }
    ]
}