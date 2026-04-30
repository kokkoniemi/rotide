interface Props {
  name: string;
  active?: boolean;
}

function Greeting({ name, active }: Props): JSX.Element {
  const label: string = active ? name : "Guest";
  return (
    <>
      <div className="greeting" data-active={active}>Hello, {label}</div>
      <span title={label} />
    </>
  );
}
