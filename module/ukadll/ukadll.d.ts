declare module 'ukadll' {
    export class UkaDll {
        /**
         * Loading a dll file and create an instance of UkaDll class.
         * 
         * Can be [SHIORI](https://ssp.shillest.net/ukadoc/manual/spec_shiori3.html)/[SAORI](http://www.boreas.dti.ne.jp/~sdn/saori.html)/[PLUGIN](https://ssp.shillest.net/ukadoc/manual/spec_plugin.html)/[HEADLINE](https://ssp.shillest.net/ukadoc/manual/spec_headline.html), follow [Ukagaka Dll Spec](https://ssp.shillest.net/ukadoc/manual/spec_dll.html) for more details.
         * @param dllPath dll file location of you want to loading
         */
        constructor(dllPath: string);
        /**
         * Will to run dll's load/loadu function
         * @param basePath Moudle's Base Path
         */
        load(basePath: string): boolean;
        /**
         * Will to run dll's unload/unload function, and release the dll file.
         * if you run this function, the instance of UkaDll will be unusable, and you should create a new instance to load the dll file again.
         */
        unload(): boolean;
        /**
         * follow [Ukagaka Dll Spec](https://ssp.shillest.net/ukadoc/manual/spec_dll.html) for more details about the request function.
         * 
         * @param rawRequest Raw [SHIORI](https://ssp.shillest.net/ukadoc/manual/spec_shiori3.html)/[SAORI](http://www.boreas.dti.ne.jp/~sdn/saori.html)/[PLUGIN](https://ssp.shillest.net/ukadoc/manual/spec_plugin.html)/[HEADLINE](https://ssp.shillest.net/ukadoc/manual/spec_headline.html) protocol request string.
         */
        request(rawRequest: string):string;
    }
}

declare module 'ukadll.dll'{
    export * from 'ukadll';
}