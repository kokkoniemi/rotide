interface Props {
  name: string;
}

function Greeting({ name }: Props): JSX.Element {
  return <div>Hello, {name}</div>;
}
