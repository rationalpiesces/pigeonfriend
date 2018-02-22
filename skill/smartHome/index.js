/*
 * please run npm install to install all dependencies
 */
var mqtt = require('mqtt') // used to send commands to broker

/*
 * Main handler
 */
exports.handler = function (request, context) {
    if (request.directive.header.namespace === 'Alexa.Discovery' && request.directive.header.name === 'Discover') {
        log("DEBUG:", "Discover request", JSON.stringify(request));
        handleDiscovery(request, context, "");
    } else if (request.directive.header.namespace === 'Alexa.PowerController') {
        if (request.directive.header.name === 'TurnOn' || request.directive.header.name === 'TurnOff') {
            log("DEBUG:", "TurnOn or TurnOff Request", JSON.stringify(request));
            handlePowerControl(request, context);
        }
    }

    /*
     * handles discovery messange from alexa
     */
    function handleDiscovery(request, context) {
        var payload = {
            "endpoints": [{
                "endpointId": "id123",
                "manufacturerName": "DIY Devices",
                "friendlyName": "Bird Feeder",
                "description": "device to feed birds in the park",
                "displayCategories": [
                    "SWITCH"
                ],
                "cookie": {},
                "capabilities": [{
                    "type": "AlexaInterface",
                    "interface": "Alexa.PowerController",
                    "version": "3",
                    "properties": {
                        "supported": [{
                            "name": "powerState"
                        }],
                        "proactivelyReported": false,
                        "retrievable": false
                    }
                }]
            }]
        };
        var header = request.directive.header;
        header.name = "Discover.Response";
        log("DEBUG", "Discovery Response: ", JSON.stringify({
            header: header,
            payload: payload
        }));
        context.succeed({
            event: {
                header: header,
                payload: payload
            }
        });
    }

    function log(message, message1, message2) {
        console.log(message + message1 + message2);
    }


    /*
     * handles turnOn messange from alexa
     */
    function handlePowerControl(request, context) {
        // get device ID passed in during discovery
        var requestMethod = request.directive.header.name;
        var powerResult;

        if (requestMethod === "TurnOn") {

            //please enter your  values here
            var broker = 'mqtt://m23.test.com:13469';
            var userId = 'mqttjs_' + Math.random().toString(16).substr(2, 8)
            var username = "username"
            var pass = 'password'

            console.log('connecting to', broker)
            var client = mqtt.connect(broker, {
                clientId: userId,
                username: username,
                password: pass
            });

            client.on('connect', function () {
                client.publish('feeder', '7', {
                    retain: true
                }, function () {
                    console.log("data published")
                    powerResult = "ON";
                    var contextResult = {
                        "properties": [{
                            "namespace": "Alexa.PowerController",
                            "name": "powerState",
                            "value": powerResult,
                            "timeOfSample": "2017-09-03T16:20:50.52Z", //retrieve from result.
                            "uncertaintyInMilliseconds": 50
                        }]
                    };
                    var responseHeader = request.directive.header;
                    responseHeader.name = "Alexa.Response";
                    responseHeader.messageId = responseHeader.messageId + "-R";
                    var response = {
                        context: contextResult,
                        event: {
                            header: responseHeader,     
                            endpoint: request.directive.endpoint,                       
                            payload: {}
                        },
                    };
                    response.event.header.name = "Response";
                    response.event.header.namespace = "Alexa";
                    log("DEBUG", "Alexa.PowerController ", JSON.stringify(response));
                    context.succeed(response);

                })
            })

        } else {
            powerResult = "OFF";
            var contextResult = {
                "properties": [{
                    "namespace": "Alexa.PowerController",
                    "name": "powerState",
                    "value": powerResult,
                    "timeOfSample": "2017-09-03T16:20:50.52Z", //retrieve from result.
                    "uncertaintyInMilliseconds": 50
                }]
            };
            var responseHeader = request.directive.header;
            responseHeader.name = "Alexa.Response";
            responseHeader.messageId = responseHeader.messageId + "-R";
            var response = {
                context: contextResult,
                event: {
                    header: responseHeader,     
                    endpoint: request.directive.endpoint,                       
                    payload: {}
                },
            };
            response.event.header.name = "Response";
            response.event.header.namespace = "Alexa";
            log("DEBUG", "Alexa.PowerController ", JSON.stringify(response));
            context.succeed(response);
        }
    }
};