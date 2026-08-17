/**
 * WASM entry point.
 *
 * Input events and the render loop are owned by the TypeScript host (src/app/).
 * The module is built with -sINVOKE_RUN=0 so main() is not invoked on load.
 */

int main()
{
   return 0;
}
