const method = {
    "SHIORI": [
        // All the methods below this comment are related to SHIORI/2.x. The current best practice does not recommend using SHIORI/2.x.
         "GET Version"
        , "GET Sentence"
        , "GET Word"
        , "GET Status"
        , "TEACH"
        , "GET String"
        , "NOTIFY OwnerGhostName"
        , "NOTIFY OtherGhostName"
        , "TRANSLATE Sentence",
        //SHIORI 3.0 ,best practice.
        "GET", "NOTIFY"
    ],
    "SAORI": [
        "GET Version", "EXECUTE"
    ],
    "PLUGIN": [
        "GET", "NOTIFY" // only implement PLUGIN/2.0
    ],
    "HEADLINE": [
        "GET Version", "GET Headline" // only implement HEADLINE/2.0
    ]
}

const protocolVersion = {
    "SHIORI": ["3.0", "2.0", "2.2", "2.3", "2.4", "2.5", "2.6"],
    "SAORI": ["1.0"],
    "PLUGIN": ["2.0"],
    "HEADLINE": ["2.0"]
}

const statusCode = {
    "SHIORI": {
        "200": "OK",
        "204": "No Content",
        "311": "Not Enough",
        "312": "Advice",
        "400": "Bad Request",
        "500": "Internal Server Error"
    },
    "SAORI": {
        "200": "OK",
        "204": "No Content",
        "400": "Bad Request",
        "500": "Internal Server Error"
    },
    "PLUGIN": {
        "200": "OK",
        "204": "No Content",
        "400": "Bad Request",
        "500": "Internal Server Error"
    },
    "HEADLINE": {
        "200": "OK",
        "204": "No Content",
        "400": "Bad Request",
        "500": "Internal Server Error"
    }
}

export class KashiwazakiParser {
    //Kashiwazaki Parser —— A Simple SHIORI Parser for QuickJS
    /**
     * 
     * @param {"SHIORI"|"SAORI"|"PLUGIN"|"HEADLINE"} protocol 
     */
    constructor(protocol) {
        this.protocol = protocol
        this.method = method[protocol]
        this.protocolVersion = protocolVersion[protocol]
    }

    /**
     * @typedef {Object} RequestLine
     * @property {string} method
     * @property {"SHIORI"|"SAORI"|"PLUGIN"|"HEADLINE"} protocol
     * @property {string} version
     */
    /**
     * 
     * @param {string} line 
     * @returns {RequestLine}
     */
    parseRequestLine(line) {
        let method = ""
        for (const requestMethod of this.method) {
            if (line.startsWith(requestMethod)) {
                method = requestMethod
                break
            }
        }
        if (method == "") {
            throw new Error("Invalid Request Line")
        }
        //check protocol
        if (!line.includes(this.protocol))
            throw new Error("Invalid Protocol")
        //version
        let version = line.split("/")[1]
        if (!this.protocolVersion.includes(version)) {
            throw new Error("Invalid Protocol Version")
        }
        return {
            "method": method,
            "protocol": this.protocol,
            "version": version
        }
    }
    /**
     * @typedef {Object} RequestObject
     * @property {RequestLine} requestLine
     * @property {Object<string,string>} headers
     * @property {string[]} [reference] //only for SHIORI and PLUGIN
     * @property {string[]} [argument] //only for SAORI
     */

    /**
     * 
     * @param {string} request 
     * @returns {}
     */
    parseRequest(request) {
        let requestArray = request.split("\r\n")
        //check last line is empty
        if (requestArray[requestArray.length - 1] != "") {
            throw new Error("Invalid Request")
        }
        requestArray = requestArray.filter((value) => value != "")
        let requestObject = {
            "requestLine": this.parseRequestLine(requestArray[0]),
            "headers": {},
        }
        //headers
        for (let i = 1; i < requestArray.length; i++) {
            let header = requestArray[i].split(":",2)
            requestObject.headers[header[0].trim()] = header[1].trim()
        }
        switch (this.protocol) {
            case "SHIORI":
            case "PLUGIN":
                requestObject.reference = [];
                for (const key in requestObject.headers) {
                    if (Object.hasOwnProperty.call(requestObject.headers, key)) {
                        const element = requestObject.headers[key];
                        if (key.startsWith("Reference")) {
                            requestObject.reference[parseInt(key.replace("Reference", ""))] = element
                            delete requestObject.headers[key]
                        }
                    }
                }
                break;
            case "SAORI": {

                /**
                 * 
                 * (Command Version)[CRLF] (例: EXECUTE SAORI/1.0)
                    SecurityLevel: (Local|External)[CRLF]
                    Argument0: Value[CRLF] (例: Argument0: GetMD5)
                    Argument1: Value[CRLF]
                    Argument2: Value[CRLF]
                        ・
                        ・
                        ・
                    Argument[n]: Value[CRLF]
                    Charset: (Shift_JIS|ISO-2022-JP|EUC-JP|UTF-8)[CRLF]
                    Sender: (ShioriName)[CRLF]
                    [CRLF]
                 */
                requestObject.argument = []
                for (const key in requestObject.headers) {
                    if (Object.hasOwnProperty.call(requestObject.headers, key)) {
                        const element = requestObject.headers[key];
                        if (key.startsWith("Argument")) {
                            requestObject.argument[parseInt(key.replace("Argument", ""))] = element
                            delete requestObject.headers[key]
                        }
                    }
                }
            }
        }
        return requestObject
    }
    /**
     * 
     * @param {RequestObject} requestObject 
     */
    buildRequest(requestObject) {
        let requestString = ""
        //headLine 
        requestString += `${requestObject.requestLine.method} ${requestObject.requestLine.protocol}/${requestObject.requestLine.version}\r\n`
        //headers
        for (const key in requestObject.headers) {
            if (Object.hasOwnProperty.call(requestObject.headers, key)) {
                const element = requestObject.headers[key];
                requestString += `${key}: ${element}\r\n`
            }
        }
        if (this.protocol == "SHIORI" || this.protocol == "PLUGIN") {
            for (let i = 0; i < requestObject.reference.length; i++) {
                const element = requestObject.reference[i];
                requestString += `Reference${i}: ${element}\r\n`
            }
        } else if (this.protocol == "SAORI") {
            /**
             * 
             * (Command Version)[CRLF] (例: EXECUTE SAORI/1.0)
                SecurityLevel: (Local|External)[CRLF]
                Argument0: Value[CRLF] (例: Argument0: GetMD5)
                Argument1: Value[CRLF]
                Argument2: Value[CRLF]
                    ・
                    ・
                    ・
                Argument[n]: Value[CRLF]
                Charset: (Shift_JIS|ISO-2022-JP|EUC-JP|UTF-8)[CRLF]
                Sender: (ShioriName)[CRLF]
                [CRLF]
             */
            for (let i = 0; i < requestObject.argument.length; i++) {
                const element = requestObject.argument[i];
                requestString += `Argument${i}: ${element}\r\n`
            }
        }
        //Terminate with a blank line ((CR+LF)x2 at the end of the last line).
        requestString += "\r\n"
        return requestString
    }

    /**
     * @typedef {Object} ResponseLine
     * @property {"SHIORI"|"PLUGIN"|"SAORI"|"HEADLINE"} protocol
     * @property {string} version
     * @property {number} statusCode
     */

    /**
     * @typedef {Object} ResponseObject
     * @property {ResponseLine} responseLine
     * @property {Object<string,string>} headers
     * @property {string[]} [reference] //only for SHIORI and PLUGIN
     * @property {string[]} [value] //only for SAORI
     * @property {string[]} [headline] //only for HEADLINE
     */

    /**
     * @typedef {Object} ShioriResponseObject
     * @property {ResponseLine} responseLine
     * @property {Object<string,string>} headers
     * @property {string[]} reference
     * @property {string} value
     */

    /**
     * @typedef {Object} PluginResponseObject
     * @property {ResponseLine} responseLine
     * @property {Object<string,string>} headers
     * @property {string[]} reference
     */

    /**
     * @typedef {Object} SaoriResponseObject
     * @property {ResponseLine} responseLine
     * @property {Object<string,string>} headers
     */

    /**
     * @typedef {Object} HeadLineResponseObject
     * @property {ResponseLine} responseLine
     * @property {Object<string,string>} headers
     * @property {string[]} headline
     */

    /**
     * Parses the first line of the response to extract protocol, version, and status code.
     * @param {string} firstLine - The first line of the response.
     */
    parseResponseLine(firstLine) {
        let responseLine = firstLine.split(" ")
        let protocol = responseLine[0].split("/")
        if (protocol.length != 2) {
            throw new Error("Invalid Response Line")
        }
        let version = protocol[1]
        let inLineStatusCode = parseInt(responseLine[1])
        
        // 首先检查状态码是否为数字
        if (isNaN(inLineStatusCode)) {
            throw new Error("Invalid Status Code: Not a Number")
        }
        
        // 获取完整的状态字符串（考虑可能包含空格）
        let statusString = responseLine.slice(2).join(" ")
        
        // 检查状态字符串是否有效
        if (!statusCode[this.protocol] || 
            !statusCode[this.protocol][inLineStatusCode.toString()] || 
            statusCode[this.protocol][inLineStatusCode.toString()] != statusString) {
            throw new Error(`Invalid Status String: ${statusString} for ${this.protocol}/${version} ${inLineStatusCode}`)
        }
        
        return {
            "protocol": protocol[0],
            "version": version,
            "statusCode": inLineStatusCode
        }
    }
    /**
     * @param {string} line response string
     * @returns {ShioriResponseObject|PluginResponseObject|SaoriResponseObject|HeadLineResponseObject}
     */
    parseResponse(line) {
        let responseArray = line.split("\r\n")
        //check last line is empty
        if (responseArray[responseArray.length - 1] != "") {
            throw new Error("Invalid Response")
        }
        responseArray = responseArray.filter((value) => value != "")
        let responseObject = {
            "responseLine": this.parseResponseLine(responseArray[0]),
            "headers": {},
        }

        switch (this.protocol) {
            case "SHIORI":
            case "PLUGIN":
            case "SAORI":
                for (let i = 1; i < responseArray.length; i++) {
                    let header = responseArray[i].split(": ")
                    responseObject.headers[header[0]] = header[1]
                }
                break;
            case "HEADLINE":
                responseObject.headline = [];
                for (let i = 1; i < responseArray.length; i++) {
                    let header = responseArray[i].split(": ")
                    if (header[0] == "Headline") {
                        responseObject.headline.push(header[1])
                    }
                }
                break;
        }

        switch (this.protocol) {
            case "SHIORI":
            case "PLUGIN":
                responseObject.reference = [];
                for (const key in responseObject.headers) {
                    if (Object.hasOwnProperty.call(responseObject.headers, key)) {
                        const element = responseObject.headers[key];
                        if (key.startsWith("Reference")) {
                            responseObject.reference[parseInt(key.replace("Reference", ""))] = element
                            delete responseObject.headers[key]
                        }
                    }
                }
                break;
            case "SAORI":
                responseObject.value = []
                for (const key in responseObject.headers) {
                    if (Object.hasOwnProperty.call(responseObject.headers, key)) {
                        const element = responseObject.headers[key];
                        if (key.startsWith("Value")) {
                            responseObject.value[parseInt(key.replace("Value", ""))] = element
                            delete responseObject.headers[key]
                        }
                    }
                }
                break;
        }
        return responseObject
    }
    /**
     * Builds a response string based on the provided responseObject.
     * @param {ShioriResponseObject|PluginResponseObject|SaoriResponseObject|HeadLineResponseObject} responseObject - The response object containing the responseLine, headers, and protocol-specific data.
     * @returns {string} - The response string.
     */
    buildResponse(responseObject) {
        let responseString = ""
        //check responseLine
        if (responseObject.responseLine.protocol != this.protocol) {
            throw new Error("Invalid Protocol")
        }
        
        // 根据协议验证响应对象类型
        switch(this.protocol) {
            case "SHIORI":
                if (!responseObject.reference) throw new Error("Invalid Shiori Response Object");
                break;
            case "PLUGIN":
                if (!responseObject.reference) throw new Error("Invalid Plugin Response Object");
                break;
            case "SAORI":
                if (!responseObject.value) throw new Error("Invalid Saori Response Object");
                break;
            case "HEADLINE":
                if (!responseObject.headline) throw new Error("Invalid HeadLine Response Object");
                break;
        }
        
        if (!this.protocolVersion.includes(responseObject.responseLine.version)) {
            throw new Error("Invalid Protocol Version")
        }
        if(!statusCode[this.protocol][responseObject.responseLine.statusCode.toString()]){
            throw new Error("Invalid Status Code")
        }
        
        ///build headLine
        responseString += `${responseObject.responseLine.protocol}/${responseObject.responseLine.version} ${responseObject.responseLine.statusCode} ${statusCode[this.protocol][responseObject.responseLine.statusCode.toString()]}\r\n`
        //headers
        for (const key in responseObject.headers) {
            if (Object.hasOwnProperty.call(responseObject.headers, key)) {
                const element = responseObject.headers[key];
                responseString += `${key}: ${element}\r\n`
            }
        }
        if (this.protocol == "SHIORI" || this.protocol == "PLUGIN") {
            for (let i = 0; i < responseObject.reference.length; i++) {
                const element = responseObject.reference[i];
                responseString += `Reference${i}: ${element}\r\n`
            }
        } else if (this.protocol == "SAORI") {
            for (let i = 0; i < responseObject.value.length; i++) {
                const element = responseObject.value[i];
                responseString += `Value${i}: ${element}\r\n`
            }
        } else if (this.protocol == "HEADLINE") {
            for (let i = 0; i < responseObject.headline.length; i++) {
                const element = responseObject.headline[i];
                responseString += `Headline: ${element}\r\n`
            }
        }
        //Terminate with a blank line ((CR+LF)x2 at the end of the last line).
        responseString += "\r\n"
        return responseString
    }
}


class Saori{
    constructor(dllPath){
        this._UkaDll = new UkaDll(dllPath)
        this._parser = new KashiwazakiParser("SAORI")
        if(!this._UkaDll.load(globalThis.__shiori_dir)){
            throw new Error("Failed to load Saori DLL")
        }
        try{
            this._getVersion()
        }catch(e){
            this._UkaDll.unload()
            throw e
        }
    }
    _getVersion(){
        let requestObject = {
            "requestLine": {
                "method": "GET Version",
                "protocol": "SAORI",
                "version": "1.0"
            },
            "headers": {
                "SecurityLevel": "Local",
                "Sender": "QuickShiori"
            },
            "argument": []
        }
        let requestString = this._parser.buildRequest(requestObject)
        let responseString = this._UkaDll.request(requestString)
        let responseObject = this._parser.parseResponse(responseString)
        if(responseObject.responseLine.statusCode != 200){
            throw new Error(`Failed to get version: ${responseObject.responseLine.statusCode}, Maybe it is not a valid Saori DLL?`)
        }
        return responseObject.value
    }
    execute(...args){
        let requestObject = {
            "requestLine": {
                "method": "EXECUTE",
                "protocol": "SAORI",
                "version": "1.0"
            },
            "headers": {
                "SecurityLevel": "Local",
                "Sender": "QuickShiori",
                "Charset": "UTF-8"
            },
            "argument": args
        }
        let requestString = this._parser.buildRequest(requestObject)
        let responseString = this._UkaDll.request(requestString)
        let responseObject = this._parser.parseResponse(responseString)
        if(responseObject.responseLine.statusCode != 200){
            throw new Error(`Failed to execute: ${responseObject.responseLine.statusCode}, Maybe it is not a valid Saori DLL?`)
        }
        return [responseObject.value, responseObject.headers]
    }
    unload(){
        if(!this._UkaDll.unload()){
            throw new Error("Failed to unload Saori DLL")
        }
    }
}