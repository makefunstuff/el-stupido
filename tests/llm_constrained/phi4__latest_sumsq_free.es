-stupido
{
    "main_body": [
        {
            "type": "assignment",
            "variable": "acc",
            "value": 0
        },
        {
            "type": "for_loop",
            "loop_variable": "i",
            "start": 1,
            "end": 100,
            "body": [
                {
                    "type": "assignment",
                    "variable": "acc",
                    "operation": "+=",
                    "expression": {
                        "type": "multiplication",
                        "operands": ["i", "i"]
                    }
                }
            ]
        },
        {
            "type": "print",
            "expression": {
                "type": "variable",
                "name": "acc"
            }
        }
    ]
}