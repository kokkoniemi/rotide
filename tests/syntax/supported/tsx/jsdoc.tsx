/**
 * Renders a greeting.
 * @param {string} name - display name.
 * @returns {JSX.Element} greeting markup.
 */
function Greeting({ name }: { name: string }): JSX.Element {
  return <div>Hello, {name}</div>;
}
